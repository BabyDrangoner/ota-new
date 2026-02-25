/**
 * test_vllm_stream.cc
 *
 * 验证 VllmClient 流式推理的实现。
 *
 * 启动一个 mock vLLM 服务（原生 POSIX socket，返回 chunked SSE），
 * 使用 VllmClient 发起请求，验证：
 *   1. 每个 token 随 chunk 到达立即触发回调（有时间间隔）
 *   2. onFirstToken 在首个 token 到达时触发
 *   3. onComplete 包含完整拼接文本和正确 token 数
 */

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include "sherry/iomanager.h"
#include "sherry/log.h"
#include "sherry/icp/icp_vllm_client.h"
#include "sherry/icp/icp_metrics.h"
#include "sherry/icp/icp_protocol.h"

using namespace sherry;
using namespace sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("icp");

static const uint16_t    MOCK_PORT = 18900;
static const std::string MOCK_HOST = "127.0.0.1";

// mock SSE tokens，每个 token 一个 chunk 发送
static const std::vector<std::string> MOCK_TOKENS = {
    "今", "天", "天", "气", "不", "错", "，",
    "阳", "光", "明", "媚", "。"
};
static const int  CHUNK_DELAY_MS   = 400;  // 每个 token 间隔，模拟推理延迟
static const uint32_t BATCH_CHUNKS = 3;    // 每次回调聚合的 chunk 数

//------------------------------------------------------------------------------
// Device channel helpers （4字节长度 + JSON，复用 IcpSession 协议）
//------------------------------------------------------------------------------

// 发送一条 OutputMessage 到 device（写端 fd）
static bool sendToDevice(int fd, const sherry::icp::OutputMessage& msg) {
    std::string json = msg.toJson();
    uint32_t len = static_cast<uint32_t>(json.size());
    if (::write(fd, &len, sizeof(len)) != sizeof(len)) return false;
    if (::write(fd, json.c_str(), json.size()) != (ssize_t)json.size()) return false;
    return true;
}

// device 线程：循环读取 4字节长度 + JSON，打印 token
struct DeviceReceiver {
    std::vector<sherry::icp::OutputMessage> msgs;  // 收到的所有消息
    std::mutex              done_mtx;
    std::condition_variable done_cv;
    std::atomic<bool>       done{false};

    void run(int recv_fd) {
        while (true) {
            uint32_t len = 0;
            ssize_t n = ::read(recv_fd, &len, sizeof(len));
            if (n <= 0) break;  // 写端关闭

            std::string json(len, '\0');
            size_t got = 0;
            while (got < len) {
                ssize_t r = ::read(recv_fd, &json[got], len - got);
                if (r <= 0) goto done_label;
                got += r;
            }

            {
                auto m = sherry::icp::OutputMessage::fromJson(json);
                if (m.type == "stream_token") {
                    std::cout << "[device] << stream_token token=[" << m.token << "]" << std::endl;
                    msgs.push_back(m);
                } else if (m.type == "stream_batch") {
                    std::cout << "[device] << stream_batch (" << m.tokens.size() << " tokens)";
                    for (auto& t : m.tokens) {
                        std::cout << " [" << t << "]";
                        // 展开为单个token记录方便验证
                        sherry::icp::OutputMessage single;
                        single.type  = "stream_token";
                        single.token = t;
                        msgs.push_back(single);
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "[device] << complete text_len=" << m.waypoints.size()
                              << " status=" << m.status << std::endl;
                    msgs.push_back(std::move(m));
                }
            }
        }
done_label:
        ::close(recv_fd);
        {
            std::lock_guard<std::mutex> lk(done_mtx);
            done = true;
        }
        done_cv.notify_one();
    }

    bool wait(int timeout_s = 30) {
        std::unique_lock<std::mutex> lk(done_mtx);
        return done_cv.wait_for(lk, std::chrono::seconds(timeout_s),
                                [this] { return done.load(); });
    }
};

static std::shared_ptr<DeviceReceiver> g_device;
//------------------------------------------------------------------------------
static void runMockServer(int server_fd) {
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int client_fd = ::accept(server_fd, (struct sockaddr*)&cli, &cli_len);
    if (client_fd < 0) {
        std::cerr << "[mock] accept failed: " << strerror(errno) << std::endl;
        return;
    }

    // 读掉 HTTP 请求
    char buf[8192] = {0};
    ::recv(client_fd, buf, sizeof(buf) - 1, 0);

    // 发送响应头
    const char* hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n";
    ::send(client_fd, hdr, strlen(hdr), 0);

    // 逐个 token 以 SSE + chunked 格式发送
    for (size_t i = 0; i < MOCK_TOKENS.size(); ++i) {
        const auto& tok = MOCK_TOKENS[i];

        // 构造 SSE payload: data: {"choices":[{"delta":{"content":"<tok>"}}]}\n\n
        std::string payload =
            "data: {\"choices\":[{\"delta\":{\"content\":\"" + tok + "\"}}]}\n\n";

        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "%zx\r\n", payload.size());
        std::string chunk = std::string(size_buf) + payload + "\r\n";

        ::send(client_fd, chunk.c_str(), chunk.size(), 0);
        std::cout << "[mock] sent token[" << i << "]=" << tok << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(CHUNK_DELAY_MS));
    }

    // [DONE] chunk
    const char* done_payload = "data: [DONE]\n\n";
    char done_size[32];
    snprintf(done_size, sizeof(done_size), "%zx\r\n", strlen(done_payload));
    std::string done_chunk = std::string(done_size) + done_payload + "\r\n";
    ::send(client_fd, done_chunk.c_str(), done_chunk.size(), 0);

    // 终止 chunk
    ::send(client_fd, "0\r\n\r\n", 5, 0);

    ::close(client_fd);
    std::cout << "[mock] done" << std::endl;
}

//------------------------------------------------------------------------------
// VllmCallback 实现
//------------------------------------------------------------------------------
struct TestCallback : public VllmCallback {
    struct TokenRecord {
        std::string token;
        int64_t     time_ms;   // 相对于首 token 的时间
    };

    std::vector<TokenRecord> tokens;
    int64_t                  first_token_abs_ms = 0;
    VllmResult               final_result;
    int                      device_send_fd = -1;  // device 写端

    std::mutex              done_mtx;
    std::condition_variable done_cv;
    std::atomic<bool>       done{false};

    void onFirstToken(const std::string& request_id, uint64_t time_ms) override {
        first_token_abs_ms = static_cast<int64_t>(time_ms);
        std::cout << "[cb] onFirstToken request_id=" << request_id
                  << " time_ms=" << time_ms << std::endl;
    }

    void onStreamBatch(const std::string& request_id,
                        const std::vector<std::string>& batch) override {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t rel = first_token_abs_ms > 0 ? (now - first_token_abs_ms) : 0;

        size_t batch_idx = tokens.size() / BATCH_CHUNKS;
        std::cout << "  --- batch " << batch_idx
                  << " (" << batch.size() << " tokens, rel_ms=" << rel << ") ---" << std::endl;
        for (auto& tok : batch) {
            std::cout << "    [cb] token=[" << tok << "]" << std::endl;
            tokens.push_back({tok, rel});
        }

        // 一次性把整批 token 发给 device
        if (device_send_fd >= 0 && !batch.empty()) {
            sherry::icp::OutputMessage msg;
            msg.car_id = 1;
            msg.seq    = tokens.size();          // 已收到的累计 token 数
            msg.type   = "stream_batch";
            msg.tokens = batch;                  // 直接放入批次
            sendToDevice(device_send_fd, msg);
        }
    }

    void onStreamToken(const std::string&, const std::string&) override {
        // 使用 onStreamBatch，单个 token 回调不需要全局处理
    }

    void onComplete(const VllmResult& result) override {
        final_result = result;
        std::cout << "[cb] onComplete status=" << (int)result.status
                  << " tokens=" << result.token_count
                  << " text=[" << result.output_text << "]" << std::endl;

        // 发送 complete 消息，然后关闭写端让 device 线程退出
        if (device_send_fd >= 0) {
            sherry::icp::OutputMessage msg;
            msg.car_id   = 1;
            msg.seq      = result.token_count;
            msg.type     = "complete";
            msg.status   = (result.status == VllmResult::Status::SUCCESS) ? "success" : "error";
            msg.waypoints = result.output_text;  // 复用 waypoints 字段传完整文本
            sendToDevice(device_send_fd, msg);
            ::close(device_send_fd);
            device_send_fd = -1;
        }

        {
            std::lock_guard<std::mutex> lk(done_mtx);
            done = true;
        }
        done_cv.notify_one();
    }

    bool wait(int timeout_s = 30) {
        std::unique_lock<std::mutex> lk(done_mtx);
        return done_cv.wait_for(lk, std::chrono::seconds(timeout_s),
                                [this] { return done.load(); });
    }
};

//------------------------------------------------------------------------------
// 主测试函数（在 IOManager fiber 中运行）
//------------------------------------------------------------------------------
static std::shared_ptr<TestCallback>  g_cb;
static std::shared_ptr<VllmClient>    g_vllm_client;  // 保证 client 生命周期超过 fiber

static void runVllmClient() {
    // 稍等服务端就绪（已在 IOManager 前建好 socket，服务端立即 accept）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    VllmConfig cfg;
    cfg.endpoint      = "http://" + MOCK_HOST + ":" + std::to_string(MOCK_PORT);
    cfg.model         = "mock-model";
    cfg.max_tokens    = 128;
    cfg.temperature   = 0.7f;
    cfg.top_p         = 0.9f;
    cfg.timeout_ms    = 30000;
    cfg.enable_stream     = true;
    cfg.stream_batch_chunks = BATCH_CHUNKS;  // 批次大小
    cfg.system_prompt = "";

    auto metrics = std::make_shared<IcpMetrics>(1);

    g_vllm_client = std::make_shared<VllmClient>(cfg, metrics, g_cb.get());
    if (!g_vllm_client->init(IOManager::GetThis())) {
        std::cerr << "[FAIL] VllmClient init failed" << std::endl;
        return;
    }

    auto req = std::make_shared<VllmRequest>();
    req->request_id          = "test-stream-001";
    req->car_id              = 1;
    req->seq                 = 0;
    req->submit_time_ms      = 0;
    req->device_timestamp_ms = 0;
    req->prompt              = "描述天气";
    // 无图片

    std::cout << "[client] submitting request..." << std::endl;
    g_vllm_client->submit(req);
}

//------------------------------------------------------------------------------
// 验证
//------------------------------------------------------------------------------
static bool verify(const TestCallback& cb) {
    bool all_pass = true;

    // 1. 检查 token 数量
    bool token_count_ok = (cb.tokens.size() == MOCK_TOKENS.size());
    std::cout << "[" << (token_count_ok ? "PASS" : "FAIL") << "] "
              << "token count=" << cb.tokens.size()
              << " expected=" << MOCK_TOKENS.size() << std::endl;
    all_pass &= token_count_ok;

    // 2. 检查 token 内容
    bool content_ok = true;
    for (size_t i = 0; i < std::min(cb.tokens.size(), MOCK_TOKENS.size()); ++i) {
        if (cb.tokens[i].token != MOCK_TOKENS[i]) {
            std::cout << "[FAIL] token[" << i << "] expected=[" << MOCK_TOKENS[i]
                      << "] got=[" << cb.tokens[i].token << "]" << std::endl;
            content_ok = false;
        }
    }
    std::cout << "[" << (content_ok ? "PASS" : "FAIL") << "] token content" << std::endl;
    all_pass &= content_ok;

    // 3. 期望批次间隔明显：batch0 首 token 和 batch1 首 token 间隔应大于 batch_size*delay*0.4
    bool batch_gap_ok = false;
    if (cb.tokens.size() >= (size_t)BATCH_CHUNKS + 1) {
        int64_t batch0_start = 0;  // first token rel=0
        int64_t batch1_start = cb.tokens[BATCH_CHUNKS].time_ms;
        int64_t gap = batch1_start - batch0_start;
        int64_t expected_min_gap = (int64_t)BATCH_CHUNKS * CHUNK_DELAY_MS * 4 / 10;
        batch_gap_ok = (gap >= expected_min_gap);
        std::cout << "[" << (batch_gap_ok ? "PASS" : "FAIL") << "] "
                  << "batch_gap=" << gap << "ms expected_min=" << expected_min_gap << "ms" << std::endl;
    } else {
        std::cout << "[FAIL] not enough tokens to verify batch gap" << std::endl;
    }
    all_pass &= batch_gap_ok;

    // 4. 检查 onComplete 结果
    bool complete_ok = (cb.final_result.status == VllmResult::Status::SUCCESS);
    std::cout << "[" << (complete_ok ? "PASS" : "FAIL") << "] "
              << "onComplete status=" << (int)cb.final_result.status << std::endl;
    all_pass &= complete_ok;

    // 5. 检查拼合文本
    std::string expected_text;
    for (auto& t : MOCK_TOKENS) expected_text += t;
    bool text_ok = (cb.final_result.output_text == expected_text);
    std::cout << "[" << (text_ok ? "PASS" : "FAIL") << "] "
              << "full_text=[" << cb.final_result.output_text << "]"
              << " expected=[" << expected_text << "]" << std::endl;
    all_pass &= text_ok;

    // 6. 检查 device 端收到的 stream_token 消息数量
    if (g_device) {
        size_t stream_cnt = 0;
        bool has_complete = false;
        std::string device_full_text;
        for (auto& m : g_device->msgs) {
            if (m.type == "stream_token") {
                ++stream_cnt;
                device_full_text += m.token;
            } else if (m.type == "complete") {
                has_complete = true;
            }
        }
        bool dev_cnt_ok = (stream_cnt == MOCK_TOKENS.size());
        std::cout << "[" << (dev_cnt_ok ? "PASS" : "FAIL") << "] "
                  << "device stream_token count=" << stream_cnt
                  << " expected=" << MOCK_TOKENS.size() << std::endl;
        all_pass &= dev_cnt_ok;

        bool dev_text_ok = (device_full_text == expected_text);
        std::cout << "[" << (dev_text_ok ? "PASS" : "FAIL") << "] "
                  << "device assembled text=[" << device_full_text << "]" << std::endl;
        all_pass &= dev_text_ok;

        std::cout << "[" << (has_complete ? "PASS" : "FAIL") << "] "
                  << "device received complete msg" << std::endl;
        all_pass &= has_complete;
    }

    return all_pass;
}

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    SYLAR_LOG_NAME("icp")->setLevel(LogLevel::INFO);
    SYLAR_LOG_NAME("system")->setLevel(LogLevel::WARN);
    SYLAR_LOG_ROOT()->setLevel(LogLevel::WARN);

    // 在 IOManager 前建好 vLLM 监听 socket
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family      = AF_INET;
    srv_addr.sin_port        = htons(MOCK_PORT);
    srv_addr.sin_addr.s_addr = inet_addr(MOCK_HOST.c_str());

    if (::bind(server_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        std::cerr << "bind failed: " << strerror(errno) << std::endl;
        return 1;
    }
    ::listen(server_fd, 5);
    std::cout << "mock vLLM server listening on "
              << MOCK_HOST << ":" << MOCK_PORT << std::endl;

    // device 通信管道：sv[0]=写端(server侧), sv[1]=读端(device侧)
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        std::cerr << "socketpair failed: " << strerror(errno) << std::endl;
        return 1;
    }

    // vLLM 服务端线程
    std::thread server_thread([server_fd]() {
        runMockServer(server_fd);
        ::close(server_fd);
    });

    // device 接收线程
    g_device = std::make_shared<DeviceReceiver>();
    std::thread device_thread([recv_fd = sv[1]]() {
        g_device->run(recv_fd);
    });

    // 初始化回调对象，传入 device 写端
    g_cb = std::make_shared<TestCallback>();
    g_cb->device_send_fd = sv[0];

    // 客户端通过 IOManager 运行
    {
        IOManager iom(2, true, "vllm_stream_test");
        iom.schedule(&runVllmClient);

        // 等待推理完成
        bool ok = g_cb->wait(30);
        if (!ok) {
            std::cerr << "[FAIL] timeout waiting for VllmClient" << std::endl;
            server_thread.join();
            return 1;
        }
    }

    server_thread.join();

    // 等待 device 线程收完（写端已在 onComplete 里 close）
    g_device->wait(10);
    device_thread.join();

    // 验证
    std::cout << "\n======= VERIFY =======" << std::endl;
    bool all_pass = verify(*g_cb);
    std::cout << "======================" << std::endl;
    std::cout << "OVERALL: " << (all_pass ? "PASS" : "FAIL") << std::endl;

    return all_pass ? 0 : 1;
}
