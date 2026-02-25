/**
 * @file icp_server_main.cc
 * @brief ICP Server 独立启动程序
 * 
 * 用法: ./icp_server [port]
 * 默认端口: 9000
 */

#include "sherry/icp/icp.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"

#include <iostream>
#include <csignal>
#include <atomic>

using namespace sherry;
using namespace sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("icp_server");
static std::atomic<bool> g_running{true};
static IcpService::ptr g_service;

void signalHandler(int signum) {
    SYLAR_LOG_INFO(g_logger) << "收到信号 " << signum << ", 正在停止服务...";
    g_running = false;
    if (g_service) {
        g_service->stop();
    }
}

int main(int argc, char** argv) {
    // 解析端口参数
    uint16_t port = 8000;
    bool enable_stream = false;
    uint32_t stream_batch_chunks = 3;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) enable_stream = (std::string(argv[2]) == "1" || std::string(argv[2]) == "true");
    if (argc > 3) stream_batch_chunks = static_cast<uint32_t>(std::atoi(argv[3]));

    // 用法提示: ./icp_server [port] [enable_stream=0/1] [stream_batch_chunks=N]

    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "       ICP Server 启动程序";
    SYLAR_LOG_INFO(g_logger) << "========================================";

    // 创建配置
    auto config = IcpConfig::getDefault();
    config->server.bind_port = port;
    config->server.bind_address = "0.0.0.0";
    
    // vLLM配置 (Qwen3-VL-4B 模型)
    config->vllm.endpoint = "http://localhost:8000";
    config->vllm.model = "Qwen/Qwen3-VL-4B";
    config->vllm.timeout_ms = 30000;
    config->vllm.max_tokens = 512;
    config->vllm.temperature = 0.7f;
    config->vllm.top_p = 0.8f;
    config->vllm.repetition_penalty = 1.05f;
    config->vllm.enable_stream       = enable_stream;        // 命令行参数控制
    config->vllm.stream_batch_chunks  = stream_batch_chunks;  // 批次大小（流式时有效）
    config->vllm.system_prompt = "You are a helpful assistant.";
    
    // 线程配置
    config->io_threads = 2;
    config->control_threads = 1;
    config->http_threads = 2;

    // 验证配置
    std::string err = config->validate();
    if (!err.empty()) {
        SYLAR_LOG_ERROR(g_logger) << "配置验证失败: " << err;
        return 1;
    }

    SYLAR_LOG_INFO(g_logger) << "配置:";
    SYLAR_LOG_INFO(g_logger) << "  - 绑定地址: " << config->server.bind_address 
                              << ":" << config->server.bind_port;
    SYLAR_LOG_INFO(g_logger) << "  - IO线程数: " << config->io_threads;
    SYLAR_LOG_INFO(g_logger) << "  - 最大连接数: " << config->server.max_connections;
    SYLAR_LOG_INFO(g_logger) << "  - 流式推理: " << (enable_stream ? "开启" : "关闭")
                              << (enable_stream ? (" batch_chunks=" + std::to_string(stream_batch_chunks)) : "");

    // 创建服务
    g_service = std::make_shared<IcpService>(config);
    
    if (!g_service->init()) {
        SYLAR_LOG_ERROR(g_logger) << "服务初始化失败";
        return 1;
    }

    if (!g_service->start()) {
        SYLAR_LOG_ERROR(g_logger) << "服务启动失败";
        return 1;
    }

    SYLAR_LOG_INFO(g_logger) << "ICP Server 已启动，监听端口 " << port;
    SYLAR_LOG_INFO(g_logger) << "按 Ctrl+C 停止服务";
    SYLAR_LOG_INFO(g_logger) << "----------------------------------------";

    // 主循环 - 等待停止信号
    while (g_running && g_service->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 停止服务
    SYLAR_LOG_INFO(g_logger) << "正在关闭服务...";
    g_service->stop();
    g_service.reset();

    SYLAR_LOG_INFO(g_logger) << "ICP Server 已停止";
    return 0;
}
