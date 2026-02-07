#include "sherry/device/device_communicator.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"
#include "sherry/http/http_connection.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ===============================
// Test MQTT Device Communicator
// ===============================
void test_mqtt_device_communicator() {
    SYLAR_LOG_INFO(g_logger) << "========== Testing MQTT Device Communicator ==========";
    
    // 创建 MQTT 设备通信器，连接到本地 MQTT broker
    auto mqtt_comm = std::make_shared<sherry::device::MqttDeviceCommunicator>(
        "localhost", 1883
    );
    
    // 连接到 MQTT broker
    mqtt_comm->connect();
    
    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    if (!mqtt_comm->is_connected()) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to connect to MQTT broker";
        return;
    }
    
    // 设置 topic 信息
    sherry::device::topicInfo topics;
    topics.pub_topic = "device/test/pub";
    topics.sub_topic = "device/test/sub";
    
    // 测试订阅
    SYLAR_LOG_INFO(g_logger) << "Testing MQTT subscribe...";
    sherry::device::subCtx sub_ctx(nullptr, 0, &topics, nullptr);
    sub_ctx.complete_cb = [](sherry::device::NET_ERROR_CODE code, sherry::device::subCtx* ctx) {
        if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
            SYLAR_LOG_INFO(g_logger) << "Subscribe callback: SUCCESS";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Subscribe callback: FAILED, code=" 
                                       << static_cast<int>(code);
        }
    };
    
    mqtt_comm->sub(sub_ctx);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 测试发布
    SYLAR_LOG_INFO(g_logger) << "Testing MQTT publish...";
    std::string test_message = "Hello from device communicator test!";
    sherry::device::pubCtx pub_ctx(
        test_message.c_str(), 
        test_message.size(), 
        &topics, 
        nullptr
    );
    pub_ctx.complete_cb = [](sherry::device::NET_ERROR_CODE code, sherry::device::pubCtx* ctx) {
        if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
            SYLAR_LOG_INFO(g_logger) << "Publish callback: SUCCESS";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Publish callback: FAILED, code=" 
                                       << static_cast<int>(code);
        }
    };
    
    mqtt_comm->pub(pub_ctx);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 再次发布到另一个 topic
    topics.pub_topic = "device/test/status";
    std::string status_msg = "{\"status\": \"online\", \"device_id\": \"test_device_001\"}";
    sherry::device::pubCtx pub_ctx2(
        status_msg.c_str(), 
        status_msg.size(), 
        &topics, 
        nullptr
    );
    pub_ctx2.complete_cb = [](sherry::device::NET_ERROR_CODE code, sherry::device::pubCtx* ctx) {
        if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
            SYLAR_LOG_INFO(g_logger) << "Publish status callback: SUCCESS";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Publish status callback: FAILED, code=" 
                                        << static_cast<int>(code);
        }
    };
    
    mqtt_comm->pub(pub_ctx2);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    SYLAR_LOG_INFO(g_logger) << "MQTT Device Communicator test completed";
}

// ===============================
// Test HTTP Device Communicator with Baidu
// ===============================
void test_http_device_communicator() {
    SYLAR_LOG_INFO(g_logger) << "========== Testing HTTP Device Communicator ==========";
    
    // 创建 IOManager
    auto io_mgr = std::make_shared<sherry::IOManager>(2, true, "test_io");
    
    // 测试 HTTP GET 请求到百度
    SYLAR_LOG_INFO(g_logger) << "Testing HTTP GET request to baidu.com...";
    
    io_mgr->schedule([]() {
        try {
            // 使用 HttpConnection 发送 GET 请求
            auto result = sherry::http::HttpConnection::DoGet(
                "http://www.baidu.com", 
                5000  // 5秒超时
            );
            
            if (result->result == 0 && result->response) {
                SYLAR_LOG_INFO(g_logger) << "HTTP GET request SUCCESS";
                SYLAR_LOG_INFO(g_logger) << "Response status code: " 
                                          << static_cast<int>(result->response->getStatus());
                SYLAR_LOG_INFO(g_logger) << "Response headers count: " 
                                          << result->response->getHeader().size();
                
                // 输出部分响应内容
                std::string body = result->response->getBody();
                if (body.size() > 200) {
                    SYLAR_LOG_INFO(g_logger) << "Response body (first 200 chars): " 
                                              << body.substr(0, 200);
                } else {
                    SYLAR_LOG_INFO(g_logger) << "Response body: " << body;
                }
            } else {
                SYLAR_LOG_ERROR(g_logger) << "HTTP GET request FAILED: " 
                                           << result->error;
            }
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "HTTP request exception: " << e.what();
        }
    });
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    io_mgr->stop();
    
    SYLAR_LOG_INFO(g_logger) << "HTTP Device Communicator test completed";
}

// ===============================
// Test HTTP with custom connection
// ===============================
void test_http_custom_connection() {
    SYLAR_LOG_INFO(g_logger) << "========== Testing HTTP Custom Connection ==========";
    
    auto io_mgr = std::make_shared<sherry::IOManager>(2, true, "test_io2");
    
    io_mgr->schedule([io_mgr]() {
        // 创建 HTTP 设备通信器
        auto http_comm = std::make_shared<sherry::device::HtppDeviceCommunicator>(
            "www.baidu.com", 80, io_mgr
        );
        
        // 连接
        http_comm->connect();
        
        if (!http_comm->is_connected()) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to connect to server";
            return;
        }
        
        // 构造 HTTP GET 请求
        std::string http_request = 
            "GET / HTTP/1.1\r\n"
            "Host: www.baidu.com\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        // 测试发送
        SYLAR_LOG_INFO(g_logger) << "Sending HTTP request...";
        sherry::device::sendCtx send_ctx(
            http_request.c_str(),
            http_request.size(),
            3  // 重试次数
        );
        send_ctx.complete_cb = [](sherry::device::NET_ERROR_CODE code, 
                                   sherry::device::sendCtx* ctx) {
            if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
                SYLAR_LOG_INFO(g_logger) << "HTTP send callback: SUCCESS";
            } else {
                SYLAR_LOG_ERROR(g_logger) << "HTTP send callback: FAILED, code=" 
                                           << static_cast<int>(code);
            }
        };
        
        http_comm->send(send_ctx);
        
        // 等待发送完成
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 测试接收
        SYLAR_LOG_INFO(g_logger) << "Receiving HTTP response...";
        char recv_buffer[4096] = {0};
        sherry::device::recvCtx recv_ctx(recv_buffer, sizeof(recv_buffer));
        recv_ctx.complete_cb = [&recv_buffer](sherry::device::NET_ERROR_CODE code,
                                               sherry::device::recvCtx* ctx) {
            if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
                SYLAR_LOG_INFO(g_logger) << "HTTP recv callback: SUCCESS";
                // 输出部分响应
                std::string response(recv_buffer, std::min(ctx->buf_size, size_t(500)));
                SYLAR_LOG_INFO(g_logger) << "Response:\n" << response;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "HTTP recv callback: FAILED, code=" 
                                           << static_cast<int>(code);
            }
        };
        
        http_comm->recv(recv_ctx);
        
        // 等待接收完成
        std::this_thread::sleep_for(std::chrono::seconds(3));
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(8));
    io_mgr->stop();
    
    SYLAR_LOG_INFO(g_logger) << "HTTP Custom Connection test completed";
}

// ===============================
// Test raw TCP Socket Device Communicator (local echo server)
// ===============================
static uint16_t launch_local_echo_server(std::thread& server_thread) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("failed to create listen socket");
    }

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        ::close(listen_fd);
        throw std::runtime_error("failed to bind echo server");
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(listen_fd, (sockaddr*)&addr, &len) != 0) {
        ::close(listen_fd);
        throw std::runtime_error("failed to getsockname");
    }
    uint16_t port = ntohs(addr.sin_port);

    if (::listen(listen_fd, 1) != 0) {
        ::close(listen_fd);
        throw std::runtime_error("failed to listen echo server");
    }

    server_thread = std::thread([listen_fd]() {
        int client_fd = ::accept(listen_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            char buf[4096];
            int n = ::recv(client_fd, buf, sizeof(buf), 0);
            if (n > 0) {
                int off = 0;
                while (off < n) {
                    int s = ::send(client_fd, buf + off, n - off, 0);
                    if (s <= 0) {
                        break;
                    }
                    off += s;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            ::close(client_fd);
        }
        ::close(listen_fd);
    });

    return port;
}

void test_socket_device_communicator() {
    SYLAR_LOG_INFO(g_logger) << "========== Testing Socket Device Communicator ==========";

    std::thread server_thread;
    uint16_t port = 0;
    try {
        port = launch_local_echo_server(server_thread);
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to launch echo server: " << e.what();
        return;
    }

    auto send_msg = std::make_shared<std::string>("hello-echo");
    auto recv_msg = std::make_shared<std::string>(send_msg->size(), '\0');

    auto sock_comm = std::make_shared<sherry::device::SocketDeviceCommunicator>(
        "127.0.0.1", (int)port, nullptr, 3000);

    sock_comm->connect();
    if (!sock_comm->is_connected()) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to connect to local echo server";
        if (server_thread.joinable()) {
            server_thread.join();
        }
        return;
    }

    sherry::device::sendCtx send_ctx(send_msg->c_str(), send_msg->size(), 1);
    send_ctx.complete_cb = [](sherry::device::NET_ERROR_CODE code, sherry::device::sendCtx*) {
        if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
            SYLAR_LOG_INFO(g_logger) << "Socket send callback: SUCCESS";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Socket send callback: FAILED, code="
                                       << static_cast<int>(code);
        }
    };
    sock_comm->send(send_ctx);

    sherry::device::recvCtx recv_ctx(recv_msg->data(), recv_msg->size());
    recv_ctx.complete_cb = [recv_msg, send_msg](sherry::device::NET_ERROR_CODE code,
                                                sherry::device::recvCtx* ctx) {
        if (code == sherry::device::NET_ERROR_CODE::SUCCESS) {
            SYLAR_LOG_INFO(g_logger) << "Socket recv callback: SUCCESS";
            std::string got(recv_msg->data(), ctx->buf_size);
            SYLAR_LOG_INFO(g_logger) << "Echo response: " << got;
            if (got != *send_msg) {
                SYLAR_LOG_ERROR(g_logger) << "Echo mismatch";
            }
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Socket recv callback: FAILED, code="
                                       << static_cast<int>(code);
        }
    };
    sock_comm->recv(recv_ctx);

    sock_comm->disconnect();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    SYLAR_LOG_INFO(g_logger) << "Socket Device Communicator test completed";
}

int main(int argc, char** argv) {
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "Device Communicator Test Starting...";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    
    // 测试 MQTT
    try {
        test_mqtt_device_communicator();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "MQTT test exception: " << e.what();
    }
    
    SYLAR_LOG_INFO(g_logger) << "\n";
    
    // 测试 HTTP (使用 DoGet)
    try {
        test_http_device_communicator();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "HTTP test exception: " << e.what();
    }
    
    SYLAR_LOG_INFO(g_logger) << "\n";
    
    // 测试 HTTP (使用自定义连接)
    try {
        test_http_custom_connection();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "HTTP custom test exception: " << e.what();
    }

    SYLAR_LOG_INFO(g_logger) << "\n";

    // 测试 Socket (本地 echo)
    try {
        test_socket_device_communicator();
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Socket test exception: " << e.what();
    }
    
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "All tests completed!";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    
    return 0;
}
