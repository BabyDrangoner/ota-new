/**
 * @file test_vllm_image.cc
 * @brief 测试 VllmClient 流式推理并按 chunk 打印
 *
 * 用法:
 *   ./bin/test_vllm_image [image_path] [vllm_endpoint] [model] [chunk_size]
 *   默认:
 *     image_path  = file/ota_1_1.0.01_gps.jpg
 *     endpoint    = http://localhost:8000
 *     model       = Qwen/Qwen3-VL-4B-Instruct
 *     chunk_size  = 10  (每积累 N 个 token 打印一包，模拟发给 device)
 *
 * 流程:
 *   vLLM SSE 流 → onStreamToken() → 累积 token buffer
 *                                  → 每满 chunk_size 个 token
 *                                    → 打印 "[→device chunk #N] <text>"
 *   推理结束 → onComplete() → 打印最后一包剩余 token (若有)
 *                            → 打印汇总信息
 */

#include "sherry/icp/icp_vllm_client.h"
#include "sherry/icp/icp_config.h"
#include "sherry/icp/icp_metrics.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"

#include <fstream>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

using namespace sherry;
using namespace sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("test");

//------------------------------------------------------------------------------
// 辅助: 从文件读取字节
//------------------------------------------------------------------------------

static std::vector<uint8_t> loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return {};
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

//------------------------------------------------------------------------------
// ChunkedCallback: 累积流式 token，每满 chunk_size 个触发一次"发送"
// 当前测试中 "发送" = 打印到终端，真实场景可替换为 write to device
//------------------------------------------------------------------------------

class ChunkedCallback : public VllmCallback {
public:
    /**
     * @param label      测试名称，用于日志区分
     * @param chunk_size 每积累多少个 token 触发一次打印 (0 = 不分包，全部收到后一次打印)
     */
    ChunkedCallback(const std::string& label, size_t chunk_size)
        : m_label(label)
        , m_chunk_size(chunk_size) {}

    // ---- VllmCallback 接口 ----

    void onFirstToken(const std::string& /*req_id*/,
                      uint64_t /*time_ms*/) override {
        m_first_token_time = std::chrono::steady_clock::now();
        std::cout << "\n[" << m_label << "] 首 token 到达，开始接收流...\n";
        std::cout.flush();
    }

    void onStreamToken(const std::string& /*req_id*/,
                       const std::string& token) override {
        // 实时回显单个 token（不换行）
        std::cout << token;
        std::cout.flush();

        // 追加到 token buffer 并计数
        std::lock_guard<std::mutex> lk(m_buf_mtx);
        m_token_buf += token;
        ++m_token_count;

        // 达到 chunk 阈值 → 模拟"发送给 device"
        if (m_chunk_size > 0 && m_token_count % m_chunk_size == 0) {
            flushChunkLocked();
        }
    }

    void onComplete(const VllmResult& result) override {
        // 换行，与上方实时 token 流分隔
        std::cout << "\n";

        // 刷出剩余不满一包的 token（尾包）
        {
            std::lock_guard<std::mutex> lk(m_buf_mtx);
            if (!m_token_buf.empty()) {
                flushChunkLocked();
            }
        }

        // 打印统计
        if (m_first_token_time.time_since_epoch().count() > 0) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_first_token_time).count();
            std::cout << "[" << m_label << "] 流结束"
                      << "  耗时(首token起)=" << elapsed_ms << "ms"
                      << "  总token=" << m_token_count
                      << "  总chunk=" << m_chunk_seq
                      << "\n";
        }

        // 通知等待线程
        std::lock_guard<std::mutex> lk(m_done_mtx);
        m_result = result;
        m_done   = true;
        m_cv.notify_one();
    }

    // 阻塞等待推理完成，timeout_s 秒内无响应返回 false
    bool wait(int timeout_s = 120) {
        std::unique_lock<std::mutex> lk(m_done_mtx);
        return m_cv.wait_for(lk, std::chrono::seconds(timeout_s),
                             [this] { return m_done.load(); });
    }

    const VllmResult& result() const { return m_result; }

private:
    // 必须在已持有 m_buf_mtx 的情况下调用
    void flushChunkLocked() {
        ++m_chunk_seq;
        // ★ 这里模拟 "发送给 device"
        // 真实场景替换为: device_conn->send(m_token_buf);
        std::cout << "\n[→device chunk #" << m_chunk_seq << "] "
                  << m_token_buf << "\n";
        std::cout.flush();
        m_token_buf.clear();
    }

private:
    std::string  m_label;
    size_t       m_chunk_size;       // 每包阈值 (token 个数)

    std::mutex   m_buf_mtx;
    std::string  m_token_buf;        // 当前未发出的 token 文本
    size_t       m_token_count{0};   // 累计接收 token 数
    size_t       m_chunk_seq{0};     // 已发出包序号

    std::chrono::steady_clock::time_point m_first_token_time{};

    std::mutex              m_done_mtx;
    std::condition_variable m_cv;
    std::atomic<bool>       m_done{false};
    VllmResult              m_result;
};

//------------------------------------------------------------------------------
// 单次完整测试 (含自己的 client + callback)
//------------------------------------------------------------------------------

static bool doTest(const std::string& case_name,
                   const VllmConfig& cfg,
                   IOManager* io_mgr,
                   const std::string& image_b64,
                   int image_count,
                   const std::string& prompt,
                   size_t chunk_size) {

    SYLAR_LOG_INFO(g_logger) << "";
    SYLAR_LOG_INFO(g_logger) << "======== " << case_name << " ========";
    SYLAR_LOG_INFO(g_logger) << "图片张数: " << image_count
                              << "  base64 每张: " << image_b64.size() << " chars";

    auto cb = std::make_shared<ChunkedCallback>(case_name, chunk_size);
    auto metrics = std::make_shared<IcpMetrics>(1);

    SYLAR_LOG_INFO(g_logger) << "chunk_size=" << chunk_size
                              << " (每" << chunk_size << "个token打印一包)";

    VllmClient client(cfg, metrics, cb.get());
    if (!client.init(io_mgr)) {
        SYLAR_LOG_ERROR(g_logger) << "[FAIL] VllmClient init failed";
        return false;
    }

    auto request = std::make_shared<VllmRequest>();
    request->request_id          = case_name + "_req";
    request->car_id              = 1;
    request->seq                 = static_cast<uint64_t>(image_count);
    request->submit_time_ms      = 0;
    request->device_timestamp_ms = 0;
    request->prompt              = prompt;

    for (int i = 0; i < image_count; ++i) {
        request->images_base64.push_back(image_b64);
    }

    SYLAR_LOG_INFO(g_logger) << "提交请求 request_id=" << request->request_id
                              << " → " << cfg.endpoint;

    client.submit(request);

    SYLAR_LOG_INFO(g_logger) << "等待流式响应 (超时 120s) chunk_size=" << chunk_size << "...";
    bool ok = cb->wait(120);

    if (!ok) {
        SYLAR_LOG_ERROR(g_logger) << "[FAIL] " << case_name << " 超时未收到响应";
        client.stop();
        return false;
    }

    const auto& res = cb->result();
    if (res.status == VllmResult::Status::SUCCESS) {
        SYLAR_LOG_INFO(g_logger) << "[PASS] " << case_name;
        SYLAR_LOG_INFO(g_logger) << "  tokens=" << res.token_count;
        SYLAR_LOG_INFO(g_logger) << "  output(前500字符)=\n"
                                  << res.output_text.substr(0, 500);
    } else {
        SYLAR_LOG_ERROR(g_logger) << "[FAIL] " << case_name
                                   << " status=" << static_cast<int>(res.status)
                                   << " error=" << res.error_message;
    }

    client.stop();
    return (res.status == VllmResult::Status::SUCCESS);
}

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------

int main(int argc, char** argv) {
    // 设置日志级别为 INFO，屏蔽 DEBUG 输出
    SYLAR_LOG_NAME("test")->setLevel(LogLevel::INFO);
    SYLAR_LOG_NAME("icp")->setLevel(LogLevel::INFO);    // VllmClient 使用
    SYLAR_LOG_NAME("system")->setLevel(LogLevel::INFO); // DeviceCamera 等使用
    SYLAR_LOG_ROOT()->setLevel(LogLevel::INFO);

    std::string image_path = "file/ota_1_1.0.01_gps.jpg";
    std::string endpoint   = "http://localhost:8000";
    std::string model      = "Qwen/Qwen3-VL-4B-Instruct";
    size_t      chunk_size = 10;   // 每满 10 个 token 打印一包

    if (argc > 1) image_path = argv[1];
    if (argc > 2) endpoint   = argv[2];
    if (argc > 3) model      = argv[3];
    if (argc > 4) chunk_size = static_cast<size_t>(std::atoi(argv[4]));

    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "   test_vllm_image  VllmClient 图片推理测试";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "图片路径:   " << image_path;
    SYLAR_LOG_INFO(g_logger) << "vLLM 端点:  " << endpoint;
    SYLAR_LOG_INFO(g_logger) << "模型:       " << model;
    SYLAR_LOG_INFO(g_logger) << "chunk_size: " << chunk_size;
    SYLAR_LOG_INFO(g_logger) << "----------------------------------------";

    // 1. 读取图片
    auto raw = loadFile(image_path);
    if (raw.empty()) {
        SYLAR_LOG_ERROR(g_logger) << "无法读取图片文件: " << image_path
                                   << "  请在项目根目录运行本测试, 或提供绝对路径";
        return 1;
    }
    SYLAR_LOG_INFO(g_logger) << "图片已加载: " << raw.size() << " bytes";

    // 2. Base64 编码
    std::string image_b64 = base64Encode(raw.data(), raw.size());
    SYLAR_LOG_INFO(g_logger) << "Base64 编码完成: " << image_b64.size() << " chars";

    // 3. 构建 vLLM 配置
    VllmConfig cfg;
    cfg.endpoint           = endpoint;
    cfg.model              = model;
    cfg.max_tokens         = 256;
    cfg.temperature        = 0.7f;
    cfg.top_p              = 0.8f;
    cfg.repetition_penalty = 1.05f;
    cfg.timeout_ms         = 120000;
    cfg.enable_stream      = true;   // 开启流式输出
    cfg.min_pixels         = 256 * 28 * 28;
    cfg.max_pixels         = 1280 * 28 * 28;
    cfg.system_prompt      = "You are a helpful assistant.";

    // 4. 创建 IOManager (用于异步 HTTP)
    auto io_mgr = std::make_shared<IOManager>(2, true, "vllm_test");

    // 5. Case 1: 单张图片
    std::string prompt_single = "请描述这张图片中的内容。";
    bool ok1 = doTest("Case1_单张图片", cfg, io_mgr.get(),
                      image_b64, 1, prompt_single, chunk_size);

    // 6. Case 2: 4张相同图片
    std::string prompt_multi  = "请依次描述这4张图片的内容，并分析是否存在异常。";
    bool ok2 = doTest("Case2_4张图片", cfg, io_mgr.get(),
                      image_b64, 4, prompt_multi, chunk_size);

    // 7. 汇总
    SYLAR_LOG_INFO(g_logger) << "";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "测试汇总:";
    SYLAR_LOG_INFO(g_logger) << "  Case1 单张图片: " << (ok1 ? "PASS" : "FAIL");
    SYLAR_LOG_INFO(g_logger) << "  Case2 4张图片:  " << (ok2 ? "PASS" : "FAIL");
    SYLAR_LOG_INFO(g_logger) << "========================================";

    io_mgr->stop();
    return (ok1 && ok2) ? 0 : 1;
}
