#include "device_communicator.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"

namespace sherry {
namespace device {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ====================================================================================
// DeviceCommnicator Base Class Implementation
// ====================================================================================

DeviceCommnicator::DeviceCommnicator(const std::string& ip, const int port)
    : m_is_connected(false)
    , m_ip(ip)
    , m_port(port) {
    SYLAR_LOG_INFO(g_logger) << "DeviceCommnicator created with IP: " << m_ip 
                              << ", Port: " << m_port;
}

DeviceCommnicator::~DeviceCommnicator() {
    SYLAR_LOG_INFO(g_logger) << "DeviceCommnicator destroyed";
}

// ====================================================================================
// MqttDeviceCommunicator Implementation
// ====================================================================================

MqttDeviceCommunicator::MqttDeviceCommunicator(const std::string& serv_ip, const int serv_port)
    : DeviceCommnicator(serv_ip, serv_port) {
    
    std::string client_id = "device_" + std::to_string(::getpid());
    
    // 创建 MQTT 客户端
    m_client = std::make_shared<MqttClient>("tcp", serv_port, serv_ip, client_id);
    
    SYLAR_LOG_INFO(g_logger) << "MqttDeviceCommunicator created with client_id: " << client_id;
}

MqttDeviceCommunicator::~MqttDeviceCommunicator() {
    if (m_is_connected && m_client) {
        m_client->disconnect();
    }
    
    // 清理 topic 列表
    for (auto& topic : m_topics) {
        if (topic) {
            delete topic;
        }
    }
    m_topics.clear();
    
    SYLAR_LOG_INFO(g_logger) << "MqttDeviceCommunicator destroyed";
}

void MqttDeviceCommunicator::connect() {
    if (m_is_connected) {
        SYLAR_LOG_WARN(g_logger) << "MqttDeviceCommunicator already connected";
        return;
    }
    
    if (!m_client) {
        SYLAR_LOG_ERROR(g_logger) << "MQTT client is null";
        return;
    }
    
    try {
        // 设置连接选项
        m_client->set_connOpts(
            60,        // Keep Alive Interval
            true,      // Auto Reconnect
            true,      // Clean Session
            "",        // Username
            "",        // Password
            10         // Connection Timeout
        );
        
        // 连接到 MQTT 服务器
        m_client->connect(true);
        m_is_connected = true;
        
        SYLAR_LOG_INFO(g_logger) << "MqttDeviceCommunicator connected to " 
                                  << m_ip << ":" << m_port;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to connect MQTT client: " << e.what();
        m_is_connected = false;
    }
}

void MqttDeviceCommunicator::disconnect() {
    if (!m_is_connected) {
        SYLAR_LOG_WARN(g_logger) << "MqttDeviceCommunicator not connected";
        return;
    }
    
    if (m_client) {
        try {
            m_client->disconnect();
            m_is_connected = false;
            SYLAR_LOG_INFO(g_logger) << "MqttDeviceCommunicator disconnected";
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to disconnect MQTT client: " << e.what();
        }
    }
}

void MqttDeviceCommunicator::pub(struct pubCtx& ctx) {
    if (!m_is_connected || !m_client) {
        SYLAR_LOG_ERROR(g_logger) << "MQTT client not connected";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::NOT_CONNECTED, &ctx);
        }
        return;
    }
    
    if (!ctx.topics || ctx.topics->pub_topic.empty()) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid pub topic";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::INVALID_PARAM, &ctx);
        }
        return;
    }
    
    if (!ctx.msg || ctx.msg_size == 0) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid message";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::INVALID_PARAM, &ctx);
        }
        return;
    }
    
    try {
        std::string message(ctx.msg, ctx.msg_size);
        std::string topic = ctx.topics->pub_topic;
        
        SYLAR_LOG_DEBUG(g_logger) << "Publishing to topic: " << topic 
                                   << ", message size: " << ctx.msg_size;
        
        // 发布消息到指定 topic，使用回调
        auto callback = [&ctx](const std::string& topic, int code) {
            if (code == 0) {
                SYLAR_LOG_INFO(g_logger) << "Published message to topic: " << topic;
                if (ctx.complete_cb) {
                    ctx.complete_cb(NET_ERROR_CODE::SUCCESS, &ctx);
                }
            } else {
                SYLAR_LOG_ERROR(g_logger) << "Failed to publish to topic: " << topic 
                                          << ", error code: " << code;
                if (ctx.complete_cb) {
                    ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
                }
            }
        };
        
        m_client->publish(topic, message, 1, false, callback);
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to publish message: " << e.what();
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
        }
    }
}

void MqttDeviceCommunicator::sub(struct subCtx& ctx) {
    if (!m_is_connected || !m_client) {
        SYLAR_LOG_ERROR(g_logger) << "MQTT client not connected";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::NOT_CONNECTED, &ctx);
        }
        return;
    }
    
    if (!ctx.topics || ctx.topics->sub_topic.empty()) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid sub topic";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::INVALID_PARAM, &ctx);
        }
        return;
    }
    
    try {
        std::string topic = ctx.topics->sub_topic;
        
        SYLAR_LOG_DEBUG(g_logger) << "Subscribing to topic: " << topic;
        
        // 订阅指定 topic，使用回调
        auto callback = [&ctx](const std::string& topic, int code) {
            if (code == 0) {
                SYLAR_LOG_INFO(g_logger) << "Subscribed to topic: " << topic;
                if (ctx.complete_cb) {
                    ctx.complete_cb(NET_ERROR_CODE::SUCCESS, &ctx);
                }
            } else {
                SYLAR_LOG_ERROR(g_logger) << "Failed to subscribe to topic: " << topic 
                                          << ", error code: " << code;
                if (ctx.complete_cb) {
                    ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
                }
            }
        };
        
        m_client->subscribe(topic, 1, callback);
        
        // 保存 topic 信息
        // topicInfo* new_topic = new topicInfo(*ctx.topics);
        // TODO: 将 new_topic 保存到 m_topics 列表中
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to subscribe: " << e.what();
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
        }
    }
}

// ====================================================================================
// HtppDeviceCommunicator Implementation
// ====================================================================================

HtppDeviceCommunicator::HtppDeviceCommunicator(const std::string serv_ip, int serv_port, IOManager::ptr io_mgr)
    : DeviceCommnicator(serv_ip, serv_port)
    , m_io_mgr(io_mgr) {
    
    SYLAR_LOG_INFO(g_logger) << "HtppDeviceCommunicator created";
}

void HtppDeviceCommunicator::connect() {
    if (m_is_connected) {
        SYLAR_LOG_WARN(g_logger) << "HtppDeviceCommunicator already connected";
        return;
    }
    
    try {
        // 创建 Socket 地址
        auto addr = sherry::IPv4Address::Create(m_ip.c_str(), m_port);
        if (!addr) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to create IPv4 address: " 
                                       << m_ip << ":" << m_port;
            return;
        }
        
        // 创建 TCP Socket
        m_sock = sherry::Socket::CreateTCP(addr);
        if (!m_sock) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to create TCP socket";
            return;
        }
        
        // 连接到服务器
        if (!m_sock->connect(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to connect to " 
                                       << m_ip << ":" << m_port;
            m_sock = nullptr;
            return;
        }
        
        // 创建 HTTP 连接
        m_connection = std::make_shared<http::HttpConnection>(m_sock);
        m_is_connected = true;
        
        SYLAR_LOG_INFO(g_logger) << "HtppDeviceCommunicator connected to " 
                                  << m_ip << ":" << m_port;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to connect: " << e.what();
        m_is_connected = false;
        m_sock = nullptr;
        m_connection = nullptr;
    }
}

void HtppDeviceCommunicator::disconnect() {
    if (!m_is_connected) {
        SYLAR_LOG_WARN(g_logger) << "HtppDeviceCommunicator not connected";
        return;
    }
    
    try {
        if (m_connection) {
            m_connection->close();
            m_connection = nullptr;
        }
        
        if (m_sock) {
            m_sock->close();
            m_sock = nullptr;
        }
        
        m_is_connected = false;
        SYLAR_LOG_INFO(g_logger) << "HtppDeviceCommunicator disconnected";
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to disconnect: " << e.what();
    }
}

void HtppDeviceCommunicator::send(struct sendCtx& ctx) {
    if (!m_is_connected || !m_connection) {
        SYLAR_LOG_ERROR(g_logger) << "HTTP connection not established";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::NOT_CONNECTED, &ctx);
        }
        return;
    }
    
    if (!ctx.msg || ctx.msg_size == 0) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid message to send";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::INVALID_PARAM, &ctx);
        }
        return;
    }
    
    // 使用 IOManager 调度发送操作
    if (!m_io_mgr) {
        SYLAR_LOG_ERROR(g_logger) << "IOManager not available";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
        }
        return;
    }
    
    int fd = m_sock->getSocket();
    size_t retry_cnt = ctx.retry_cnt;
    
    // 添加写事件到 IOManager
    m_io_mgr->addEvent(fd, IOManager::WRITE, [this, &ctx, retry_cnt]() mutable {
        SYLAR_LOG_DEBUG(g_logger) << "Sending HTTP message, size: " << ctx.msg_size 
                                   << ", retry left: " << retry_cnt;
        
        try {
            // 发送数据
            int ret = m_connection->writeFixSize(ctx.msg, ctx.msg_size);
            if (ret <= 0) {
                SYLAR_LOG_ERROR(g_logger) << "Failed to send data, ret: " << ret;
                
                // 失败时尝试重发
                if (retry_cnt > 0) {
                    SYLAR_LOG_INFO(g_logger) << "Retrying send, attempts left: " << retry_cnt;
                    retry_cnt--;
                    
                    // 重新添加写事件
                    int fd = m_sock->getSocket();
                    m_io_mgr->addEvent(fd, IOManager::WRITE, [this, &ctx, retry_cnt]() {
                        send(ctx);
                    });
                } else {
                    SYLAR_LOG_ERROR(g_logger) << "Retry exhausted";
                    m_is_connected = false;
                    if (ctx.complete_cb) {
                        ctx.complete_cb(NET_ERROR_CODE::RETRY_EXHAUSTED, &ctx);
                    }
                }
                return;
            }
            
            SYLAR_LOG_INFO(g_logger) << "Sent " << ret << " bytes via HTTP";
            if (ctx.complete_cb) {
                ctx.complete_cb(NET_ERROR_CODE::SUCCESS, &ctx);
            }
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to send data: " << e.what();
            m_is_connected = false;
            if (ctx.complete_cb) {
                ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
            }
        }
    });
}

void HtppDeviceCommunicator::recv(struct recvCtx& ctx) {
    if (!m_is_connected || !m_connection) {
        SYLAR_LOG_ERROR(g_logger) << "HTTP connection not established";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::NOT_CONNECTED, &ctx);
        }
        return;
    }
    
    if (!ctx.buf || ctx.buf_size == 0) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid buffer for receive";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::INVALID_PARAM, &ctx);
        }
        return;
    }
    
    // 使用 IOManager 调度接收操作
    if (!m_io_mgr) {
        SYLAR_LOG_ERROR(g_logger) << "IOManager not available";
        if (ctx.complete_cb) {
            ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
        }
        return;
    }
    
    int fd = m_sock->getSocket();
    
    // 添加读事件到 IOManager
    m_io_mgr->addEvent(fd, IOManager::READ, [this, &ctx]() {
        SYLAR_LOG_DEBUG(g_logger) << "Receiving HTTP message, buffer size: " << ctx.buf_size;
        
        try {
            // 接收数据
            int ret = m_connection->readFixSize((void*)ctx.buf, ctx.buf_size);
            if (ret <= 0) {
                SYLAR_LOG_ERROR(g_logger) << "Failed to receive data, ret: " << ret;
                m_is_connected = false;
                if (ctx.complete_cb) {
                    ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
                }
                return;
            }
            
            SYLAR_LOG_INFO(g_logger) << "Received " << ret << " bytes via HTTP";
            if (ctx.complete_cb) {
                ctx.complete_cb(NET_ERROR_CODE::SUCCESS, &ctx);
            }
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to receive data: " << e.what();
            m_is_connected = false;
            if (ctx.complete_cb) {
                ctx.complete_cb(NET_ERROR_CODE::FAILED, &ctx);
            }
        }
    });
}

} // namespace device
} // namespace sherry
