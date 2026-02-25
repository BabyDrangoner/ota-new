/**
 * @brief test_chunk_stream.cc
 *
 * 测试 HTTP chunked 流式接收。
 *
 * 结构：
 *   - 服务端：在 IOManager 中启动一个监听 socket，接受连接后
 *             手动写入 HTTP/1.1 chunked 响应（共 6 个 chunk），
 *             每个 chunk 之间短暂延迟模拟推理延迟。
 *   - 客户端：使用 HttpConnection::DoPostStream 以 ChunkPolicy
 *             {max_chunks=2} 接收，每满 2 个 chunk 触发一次回调。
 *
 * 预期：
 *   - 共回调 4 次：每 2 个 chunk 触发一次（共 3 批数据）
 *               + 1 次空的 is_done=true 结束信号
 *   - is_done=true 仅最后一次
 *   - 所有 chunk 数据拼合后等于原始内容
 */

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "sherry/iomanager.h"
#include "sherry/socket.h"
#include "sherry/address.h"
#include "sherry/log.h"
#include "sherry/http/http_connection.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static const uint16_t PORT = 18765;
static const std::string HOST = "127.0.0.1";

// 服务端：使用原生 POSIX socket（在 std::thread 中运行）
static void runServer(int server_fd) {
    SYLAR_LOG_INFO(g_logger) << "[server] waiting for connection...";
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int client_fd = ::accept(server_fd, (struct sockaddr*)&cli_addr, &cli_len);
    if (client_fd < 0) {
        SYLAR_LOG_ERROR(g_logger) << "[server] accept failed: " << strerror(errno);
        return;
    }
    SYLAR_LOG_INFO(g_logger) << "[server] accepted client fd=" << client_fd;

    // 1. 读掉客户端发来的 HTTP 请求（不关心内容）
    char req_buf[4096] = {0};
    ::recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);

    // 2. 发送 HTTP 响应头
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n";
    ::send(client_fd, header.c_str(), header.size(), 0);
    SYLAR_LOG_INFO(g_logger) << "[server] sent response header";

    // 3. 模拟 6 个 SSE-like chunk，间隔 30ms
    std::vector<std::string> payloads = {
        "data: token-1\n\n",
        "data: token-2\n\n",
        "data: token-3\n\n",
        "data: token-4\n\n",
        "data: token-5\n\n",
        "data: token-6\n\n",
    };

    for (size_t i = 0; i < payloads.size(); ++i) {
        const auto& p = payloads[i];
        char size_line[32];
        snprintf(size_line, sizeof(size_line), "%zx\r\n", p.size());
        std::string chunk = std::string(size_line) + p + "\r\n";
        ::send(client_fd, chunk.c_str(), chunk.size(), 0);
        SYLAR_LOG_INFO(g_logger) << "[server] sent chunk " << (i + 1)
                                  << " (" << p.size() << " bytes)";
        // 模拟推理延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    // 4. 终止 chunk
    const char* terminator = "0\r\n\r\n";
    ::send(client_fd, terminator, strlen(terminator), 0);
    SYLAR_LOG_INFO(g_logger) << "[server] sent terminator";

    ::close(client_fd);
    SYLAR_LOG_INFO(g_logger) << "[server] done";
}

// 客户端：使用 DoPostStream 接收
static void runClient() {
    // 等待服务端就绪
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    sherry::http::HttpConnection::ChunkPolicy policy;
    policy.max_chunks = 2;   // 每 2 个 chunk 触发一次回调

    std::vector<std::string> batches;      // 每批回调收到的 data
    std::vector<bool>        done_flags;   // 对应的 is_done
    std::mutex               mu;
    std::string              all_data;

    SYLAR_LOG_INFO(g_logger) << "[client] sending POST to http://"
                              << HOST << ":" << PORT << "/stream";

    auto result = sherry::http::HttpConnection::DoPostStream(
        "http://" + HOST + ":" + std::to_string(PORT) + "/stream",
        5000,
        {{"Content-Type", "application/json"}},
        "{\"prompt\":\"hello\"}",
        policy,
        [&](const std::string& data, bool is_done) {
            std::lock_guard<std::mutex> lk(mu);
            batches.push_back(data);
            done_flags.push_back(is_done);
            all_data += data;
            // 打印时间戳，直观感受分批到达
            auto now = std::chrono::system_clock::now();
            auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now.time_since_epoch()).count();
            SYLAR_LOG_INFO(g_logger) << "[client] batch #" << batches.size()
                                      << "  time=" << ms
                                      << "  size=" << data.size()
                                      << "  is_done=" << is_done
                                      << "\n" << data;
        }
    );

    // ---- 验证 ----
    SYLAR_LOG_INFO(g_logger) << "======= RESULT =======";
    if (!result || result->result != (int)sherry::http::HttpResult::Error::OK) {
        SYLAR_LOG_ERROR(g_logger) << "[FAIL] HTTP request failed: "
                                   << (result ? result->error : "null result");
        return;
    }
    SYLAR_LOG_INFO(g_logger) << "[client] HTTP status: " << (int)result->response->getStatus();
    SYLAR_LOG_INFO(g_logger) << "[client] total batches: " << batches.size();
    SYLAR_LOG_INFO(g_logger) << "[client] all_data:\n" << all_data;

    // 最后一次 is_done 必须为 true
    bool last_done = !done_flags.empty() && done_flags.back();
    SYLAR_LOG_INFO(g_logger) << "[" << (last_done ? "PASS" : "FAIL")
                              << "] last callback is_done=" << last_done;

    // 中间回调不能是 done
    bool mid_ok = true;
    for (size_t i = 0; i + 1 < done_flags.size(); ++i) {
        if (done_flags[i]) { mid_ok = false; break; }
    }
    SYLAR_LOG_INFO(g_logger) << "[" << (mid_ok ? "PASS" : "FAIL")
                              << "] no intermediate done";

    // 所有数据拼合后包含 6 个 token
    bool data_ok = true;
    for (int i = 1; i <= 6; ++i) {
        std::string expected = "token-" + std::to_string(i);
        if (all_data.find(expected) == std::string::npos) {
            data_ok = false;
            SYLAR_LOG_ERROR(g_logger) << "[FAIL] missing " << expected;
        }
    }
    SYLAR_LOG_INFO(g_logger) << "[" << (data_ok ? "PASS" : "FAIL")
                              << "] all 6 tokens received";

    // 批次数量：6 chunk / max_chunks=2 → 3 批数据 + 1 次空 done = 4
    // （当数据批次正好整除时，终止 chunk 会单独触发一次空 is_done信号）
    bool batch_ok = (batches.size() == 4);
    SYLAR_LOG_INFO(g_logger) << "[" << (batch_ok ? "PASS" : "FAIL")
                              << "] batch count=" << batches.size() << " (expected 4: 3 data + 1 empty done)";
}

void test_chunk_stream() {
    // 客户端在 IOManager fiber 内运行，等服务端就绪
    runClient();
}

int main(int argc, char* argv[]) {
    // 只输出 INFO 及以上日志
    sherry::LoggerMgr::GetInstance()->getRoot()->setLevel(sherry::LogLevel::INFO);
    SYLAR_LOG_NAME("system")->setLevel(sherry::LogLevel::INFO);
    // 服务端 socket 在 IOManager 启动前（主线程）创建，避免被 hook 成非阻塞
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket failed: " << strerror(errno) << std::endl;
        return 1;
    }
    int reuse = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family      = AF_INET;
    srv_addr.sin_port        = htons(PORT);
    srv_addr.sin_addr.s_addr = inet_addr(HOST.c_str());

    if (::bind(server_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        std::cerr << "bind failed: " << strerror(errno) << std::endl;
        return 1;
    }
    ::listen(server_fd, 5);
    std::cout << "server listening on " << HOST << ":" << PORT << std::endl;

    // 服务端线程
    std::thread server_thread([server_fd]() {
        runServer(server_fd);
        ::close(server_fd);
    });

    // 客户端通过 IOManager 运行
    {
        sherry::IOManager iom(2, true, "test_chunk");
        iom.schedule(&test_chunk_stream);
    }   // IOManager 析构时等待所有 fiber 完成

    server_thread.join();
    return 0;
}
