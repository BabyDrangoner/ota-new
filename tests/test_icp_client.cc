/**
 * @file test_icp_client.cc
 * @brief ICP 客户端模拟器
 * 
 * 模拟设备端发送图像消息到ICP服务器
 */

#include "sherry/icp/icp.h"
#include "sherry/socket.h"
#include "sherry/address.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <random>

using namespace sherry;
using namespace sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("client");

class IcpClient {
public:
    IcpClient(uint32_t car_id, const std::string& server_addr, uint16_t port)
        : m_carId(car_id)
        , m_serverAddr(server_addr)
        , m_port(port)
        , m_seq(0)
        , m_running(false) {
    }
    
    bool connect() {
        auto addr = Address::LookupAnyIPAddress(
            m_serverAddr + ":" + std::to_string(m_port));
        if (!addr) {
            SYLAR_LOG_ERROR(g_logger) << "Invalid server address";
            return false;
        }
        
        m_socket = Socket::CreateTCPSocket();
        if (!m_socket->connect(addr, 5000)) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to connect to " << addr->toString();
            return false;
        }
        
        SYLAR_LOG_INFO(g_logger) << "Connected to " << addr->toString();
        return true;
    }
    
    void disconnect() {
        m_running = false;
        if (m_socket) {
            m_socket->close();
        }
    }
    
    bool sendMessage(size_t image_size = 1000, const std::string& prompt = "") {
        if (!m_socket || !m_socket->isConnected()) {
            return false;
        }
        
        // 构建消息
        MessageBuilder builder;
        builder.setCarId(m_carId)
               .setSeq(++m_seq)
               .setTimestamp(getSystemTimeMs());
        
        if (!prompt.empty()) {
            builder.setPrompt(prompt);
        }
        
        // 生成假图片数据
        std::vector<uint8_t> fake_image(image_size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& byte : fake_image) {
            byte = static_cast<uint8_t>(dis(gen));
        }
        
        builder.addImage(ImageType::RGB, fake_image.data(), fake_image.size());
        
        auto data = builder.build();
        
        // 发送
        int ret = m_socket->send(data.data(), data.size());
        if (ret != static_cast<int>(data.size())) {
            SYLAR_LOG_ERROR(g_logger) << "Send failed: " << ret;
            return false;
        }
        
        SYLAR_LOG_DEBUG(g_logger) << "Sent message: car_id=" << m_carId
                                   << " seq=" << m_seq
                                   << " size=" << data.size();
        return true;
    }
    
    std::string receiveResult() {
        if (!m_socket || !m_socket->isConnected()) {
            return "";
        }
        
        char buffer[4096];
        int ret = m_socket->recv(buffer, sizeof(buffer) - 1);
        if (ret > 0) {
            buffer[ret] = '\0';
            return std::string(buffer, ret);
        }
        return "";
    }
    
    void runSimulation(int msg_count, int interval_ms) {
        m_running = true;
        
        for (int i = 0; i < msg_count && m_running; ++i) {
            if (!sendMessage(1000, "Analyze and provide waypoints")) {
                break;
            }
            
            if (interval_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            }
        }
    }
    
    void startReceiver() {
        std::thread([this]() {
            while (m_running && m_socket && m_socket->isConnected()) {
                std::string result = receiveResult();
                if (!result.empty()) {
                    SYLAR_LOG_INFO(g_logger) << "Received result: " << result;
                }
            }
        }).detach();
    }
    
private:
    uint32_t m_carId;
    std::string m_serverAddr;
    uint16_t m_port;
    uint64_t m_seq;
    Socket::ptr m_socket;
    std::atomic<bool> m_running;
};

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -h, --help          Show this help\n"
              << "  -s, --server ADDR   Server address (default: 127.0.0.1)\n"
              << "  -p, --port PORT     Server port (default: 9000)\n"
              << "  -c, --car-id ID     Car ID (default: 0)\n"
              << "  -n, --count N       Number of messages (default: 10)\n"
              << "  -i, --interval MS   Interval between messages in ms (default: 100)\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    g_logger->setLevel(LogLevel::DEBUG);
    
    std::string server = "127.0.0.1";
    uint16_t port = 9000;
    uint32_t car_id = 0;
    int count = 10;
    int interval = 100;
    
    // 简单的参数解析
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-s" || arg == "--server") && i + 1 < argc) {
            server = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "-c" || arg == "--car-id") && i + 1 < argc) {
            car_id = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if ((arg == "-n" || arg == "--count") && i + 1 < argc) {
            count = std::stoi(argv[++i]);
        } else if ((arg == "-i" || arg == "--interval") && i + 1 < argc) {
            interval = std::stoi(argv[++i]);
        }
    }
    
    SYLAR_LOG_INFO(g_logger) << "ICP Client starting...";
    SYLAR_LOG_INFO(g_logger) << "Server: " << server << ":" << port;
    SYLAR_LOG_INFO(g_logger) << "Car ID: " << car_id;
    SYLAR_LOG_INFO(g_logger) << "Messages: " << count << " @ " << interval << "ms";
    
    IOManager iom(1, true, "client");
    
    iom.schedule([&]() {
        IcpClient client(car_id, server, port);
        
        if (!client.connect()) {
            SYLAR_LOG_ERROR(g_logger) << "Connection failed";
            return;
        }
        
        client.startReceiver();
        client.runSimulation(count, interval);
        
        // 等待接收结果
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        client.disconnect();
        SYLAR_LOG_INFO(g_logger) << "Client finished";
    });
    
    return 0;
}
