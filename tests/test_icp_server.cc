/**
 * @file test_icp_server.cc
 * @brief ICP 服务器集成测试
 * 
 * 启动完整的ICP服务并等待连接
 */

#include "sherry/icp/icp.h"
#include "sherry/log.h"

#include <iostream>
#include <csignal>
#include <thread>

using namespace sherry;
using namespace sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static IcpService::ptr g_service;

void signalHandler(int signo) {
    SYLAR_LOG_INFO(g_logger) << "Signal " << signo << " received, stopping...";
    if (g_service) {
        g_service->stop();
    }
}

int main(int argc, char* argv[]) {
    // 设置日志级别
    g_logger->setLevel(LogLevel::DEBUG);
    
    SYLAR_LOG_INFO(g_logger) << "ICP Server starting...";
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 加载配置
    IcpConfig::ptr config;
    if (argc > 1) {
        config = IcpConfig::loadFromFile(argv[1]);
        if (!config) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to load config: " << argv[1];
            return 1;
        }
    } else {
        config = IcpConfig::getDefault();
        SYLAR_LOG_INFO(g_logger) << "Using default config";
    }
    
    // 调整配置用于测试
    config->server.bind_port = 9000;
    config->max_cars = 10;
    config->io_threads = 2;
    config->control_threads = 1;
    config->http_threads = 2;
    config->enable_metrics = true;
    config->vllm.endpoint = "http://localhost:8000";
    
    // 创建服务
    g_service = std::make_shared<IcpService>(config);
    
    // 初始化
    if (!g_service->init()) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to init ICP service";
        return 1;
    }
    
    // 启动
    if (!g_service->start()) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to start ICP service";
        return 1;
    }
    
    SYLAR_LOG_INFO(g_logger) << "ICP Server running on port " 
                              << config->server.bind_port;
    SYLAR_LOG_INFO(g_logger) << "Press Ctrl+C to stop";
    
    // 定期输出统计信息
    while (g_service->isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        if (g_service->isRunning()) {
            auto metrics = g_service->getMetrics();
            if (metrics) {
                SYLAR_LOG_INFO(g_logger) << metrics->generateSummary();
            }
        }
    }
    
    SYLAR_LOG_INFO(g_logger) << "ICP Server stopped";
    return 0;
}
