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
#include <fstream>
#include <unistd.h>

using namespace sherry;
using namespace sherry::device;

static Logger::ptr g_logger = SYLAR_LOG_NAME("device_client");
static std::atomic<bool> g_running{true};
static DeviceEngine::ptr g_device;

void signalHandler(int signum) {
    std::cout << "\n收到信号 " << signum << ", 正在停止设备...\n";
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
    std::string image_path = "file/ota_1_1.0.01_gps.jpg";  // 默认使用 file/ 目录下的图片

    if (argc > 1) server_ip = argv[1];
    if (argc > 2) server_port = static_cast<uint16_t>(std::atoi(argv[2]));
    if (argc > 3) car_id = static_cast<uint64_t>(std::atoll(argv[3]));
    if (argc > 4) image_path = argv[4];

    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "========================================\n"
              << "       设备客户端启动程序\n"
              << "========================================\n"
              << "配置:\n"
              << "  - 服务器地址: " << server_ip << ":" << server_port << "\n"
              << "  - 车辆ID: " << car_id << "\n"
              << "  - 图片路径: " << image_path << "\n";

    // 创建 IOManager
    auto io_mgr = std::make_shared<IOManager>(2, true, "device");

    // 创建相机，加载 file/ 目录下的图片作为 RGB 输入 (无 DEPTH)
    {
        std::ifstream probe(image_path, std::ios::binary);
        if (!probe.is_open()) {
            char cwd_buf[4096] = {};
            getcwd(cwd_buf, sizeof(cwd_buf));
            std::cerr << "[ERROR] 图片文件不存在或无法打开: " << image_path
                      << "  (当前工作目录: " << cwd_buf << ")\n";
            return 1;
        }
    }
    auto camera = std::make_shared<SingleCamera>(1, 0, image_path);
    std::cout << "  - 相机: 1 RGB (来自文件), 0 DEPTH\n"
              << "  - 图像缓冲区大小: " << camera->get_buf_len() << " bytes\n";

    // 配置设备引擎
    DeviceEngine::Options opts;
    opts.server_ip = server_ip;
    opts.server_port = server_port;
    opts.car_id = car_id;
    opts.send_interval_ms = 2000;  // 每2秒发送一次
    opts.connect_timeout_ms = 5000;
    opts.max_payload_bytes = 16 * 1024 * 1024;
    opts.listen_transport = DeviceEngine::Transport::SocketTcp;

    std::cout << "  - 发送间隔: " << opts.send_interval_ms << " ms\n"
              << "----------------------------------------\n";

    // 创建设备引擎
    g_device = std::make_shared<DeviceEngine>(opts, camera, io_mgr);

    // 统计计数器
    std::atomic<uint64_t> msg_sent{0};
    std::atomic<uint64_t> result_received{0};

    // 跟踪当前流式请求
    uint64_t stream_seq = 0;
    bool     stream_open = false;  // 是否正在打印某个请求的 token 流

    // 设置 ICP 结果回调
    g_device->setIcpResultCallback([&](const icp::OutputMessage& msg) {
        ++result_received;
        const std::string& type = msg.type;

        if (type == "stream_batch" || type == "stream_token") {
            // 新请求：打印请求头
            if (!stream_open || stream_seq != msg.seq) {
                if (stream_open) {
                    // 上一个请求未收到 complete，先换行
                    std::cout << "\n";
                }
                std::cout << "\n>>> [car=" << msg.car_id
                          << " seq=" << msg.seq << "]\n";
                stream_seq  = msg.seq;
                stream_open = true;
            }

            // 内联打印 token
            if (type == "stream_batch") {
                for (auto& tok : msg.tokens) {
                    std::cout << tok;
                }
            } else {
                std::cout << msg.token;
            }
            std::cout.flush();

        } else {
            // complete 帧
            if (stream_open && stream_seq == msg.seq) {
                // 结束当前 token 流行
                std::cout << "\n";
                stream_open = false;
            }

            std::string result_line = msg.status == "success" ? "OK" : ("ERR:" + msg.status);
            std::cout << "<<< [car=" << msg.car_id
                      << " seq=" << msg.seq << "] "
                      << result_line
                      << "  latency=" << msg.latency_ms << "ms\n";
            std::cout.flush();
        }
    });

    // 设置服务器消息回调（仅 debug 级别，默认不可见）
    g_device->setServerMessageCallback([](const std::string& msg) {
        SYLAR_LOG_DEBUG(g_logger) << "[Server Raw] " << msg;
    });

    // 启动设备
    if (!g_device->start()) {
        std::cerr << "[ERROR] 设备启动失败\n";
        return 1;
    }

    std::cout << "设备已启动，正在连接服务器...\n"
              << "按 Ctrl+C 停止设备\n";

    // 主循环 - 打印状态
    auto last_print = std::chrono::steady_clock::now();
    while (g_running && g_device->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print);
        if (elapsed.count() >= 5) {
            std::cout << "[状态] 消息已发送: " << msg_sent
                      << "  结果已接收: " << result_received << "\n";
            last_print = now;
        }
    }

    // 停止设备
    std::cout << "正在关闭设备...\n";
    g_device->stop();
    g_device.reset();

    // 停止 IOManager
    io_mgr->stop();

    std::cout << "设备客户端已停止\n"
              << "统计: 发送=" << msg_sent << "  接收=" << result_received << "\n";
    return 0;
}
