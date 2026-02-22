/**
 * @file device_client_main.cc
 * @brief 设备客户端独立启动程序
 * 
 * 用法: ./device_client [server_ip] [server_port] [car_id]
 * 默认: 127.0.0.1:9000 car_id=1
 */

#include "sherry/device/device_engine.h"
#include "sherry/device/device_camera.h"
#include "sherry/icp/icp_protocol.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>

using namespace sherry;
using namespace sherry::device;

static Logger::ptr g_logger = SYLAR_LOG_NAME("device_client");
static std::atomic<bool> g_running{true};
static DeviceEngine::ptr g_device;

void signalHandler(int signum) {
    SYLAR_LOG_INFO(g_logger) << "收到信号 " << signum << ", 正在停止设备...";
    g_running = false;
    if (g_device) {
        g_device->stop();
    }
}

int main(int argc, char** argv) {
    // 解析参数
    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 8000;
    uint64_t car_id = 1;

    if (argc > 1) server_ip = argv[1];
    if (argc > 2) server_port = static_cast<uint16_t>(std::atoi(argv[2]));
    if (argc > 3) car_id = static_cast<uint64_t>(std::atoll(argv[3]));

    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "       设备客户端启动程序";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "配置:";
    SYLAR_LOG_INFO(g_logger) << "  - 服务器地址: " << server_ip << ":" << server_port;
    SYLAR_LOG_INFO(g_logger) << "  - 车辆ID: " << car_id;

    // 创建 IOManager
    auto io_mgr = std::make_shared<IOManager>(2, true, "device");

    // 创建模拟相机 (1张RGB + 1张深度图)
    auto camera = std::make_shared<SingleCamera>(1, 1);
    SYLAR_LOG_INFO(g_logger) << "  - 相机: 1 RGB + 1 DEPTH";
    SYLAR_LOG_INFO(g_logger) << "  - 图像缓冲区大小: " << camera->get_buf_len() << " bytes";

    // 配置设备引擎
    DeviceEngine::Options opts;
    opts.server_ip = server_ip;
    opts.server_port = server_port;
    opts.car_id = car_id;
    opts.send_interval_ms = 2000;  // 每2秒发送一次
    opts.connect_timeout_ms = 5000;
    opts.max_payload_bytes = 16 * 1024 * 1024;
    opts.listen_transport = DeviceEngine::Transport::SocketTcp;

    SYLAR_LOG_INFO(g_logger) << "  - 发送间隔: " << opts.send_interval_ms << " ms";
    SYLAR_LOG_INFO(g_logger) << "----------------------------------------";

    // 创建设备引擎
    g_device = std::make_shared<DeviceEngine>(opts, camera, io_mgr);

    // 统计计数器
    std::atomic<uint64_t> msg_sent{0};
    std::atomic<uint64_t> result_received{0};

    // 设置 ICP 结果回调
    g_device->setIcpResultCallback([&](const icp::OutputMessage& msg) {
        ++result_received;
        SYLAR_LOG_INFO(g_logger) << "[ICP Result] "
                                  << "car_id=" << msg.car_id
                                  << " seq=" << msg.seq
                                  << " status=" << msg.status
                                  << " latency=" << msg.latency_ms << "ms";
        
        if (!msg.waypoints.empty()) {
            SYLAR_LOG_INFO(g_logger) << "  路径点(JSON): " << msg.waypoints;
        }
    });

    // 设置服务器消息回调
    g_device->setServerMessageCallback([](const std::string& msg) {
        SYLAR_LOG_DEBUG(g_logger) << "[Server Raw] " << msg;
    });

    // 启动设备
    if (!g_device->start()) {
        SYLAR_LOG_ERROR(g_logger) << "设备启动失败";
        return 1;
    }

    SYLAR_LOG_INFO(g_logger) << "设备已启动，正在连接服务器...";
    SYLAR_LOG_INFO(g_logger) << "按 Ctrl+C 停止设备";

    // 主循环 - 打印状态
    auto last_print = std::chrono::steady_clock::now();
    while (g_running && g_device->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print);
        if (elapsed.count() >= 5) {
            SYLAR_LOG_INFO(g_logger) << "[状态] 消息已发送: " << msg_sent 
                                      << " 结果已接收: " << result_received;
            last_print = now;
        }
    }

    // 停止设备
    SYLAR_LOG_INFO(g_logger) << "正在关闭设备...";
    g_device->stop();
    g_device.reset();

    // 停止 IOManager
    io_mgr->stop();

    SYLAR_LOG_INFO(g_logger) << "设备客户端已停止";
    SYLAR_LOG_INFO(g_logger) << "统计: 发送=" << msg_sent << " 接收=" << result_received;
    return 0;
}
