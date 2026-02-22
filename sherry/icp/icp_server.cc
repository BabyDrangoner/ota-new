#include "icp_server.h"
#include "sherry/log.h"
#include "sherry/address.h"

namespace sherry {
namespace icp {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("icp");

//------------------------------------------------------------------------------
// IcpSession
//------------------------------------------------------------------------------

IcpSession::IcpSession(Socket::ptr socket, IcpController::ptr controller)
    : m_socket(socket)
    , m_controller(controller)
    , m_recvPos(0) {
    m_stream = std::make_shared<SocketStream>(socket);
    
    // 预分配接收缓冲区
    auto config = controller->getConfig();
    m_recvBuffer.resize(config->max_msg_size);
}

void IcpSession::setRegisterCallbacks(RegisterCb on_register,
                                      UnregisterCb on_unregister) {
    m_onRegister   = std::move(on_register);
    m_onUnregister = std::move(on_unregister);
}

IcpSession::~IcpSession() {
    close();
}

void IcpSession::start() {
    // 记录连接
    if (m_controller->getMetrics()) {
        m_controller->getMetrics()->recordConnectionOpen();
    }
    
    SYLAR_LOG_INFO(g_logger) << "IcpSession started: " 
                              << m_socket->getRemoteAddress()->toString();
    
    // 开始处理循环
    handleLoop();
}

bool IcpSession::sendResult(const OutputMessage& msg) {
    if (!isConnected()) {
        return false;
    }
    
    Mutex::Lock lock(m_writeMutex);
    
    // 序列化为 JSON
    std::string json = msg.toJson();
    
    // 发送格式: 4字节长度 + JSON数据
    uint32_t len = static_cast<uint32_t>(json.size());
    
    int ret = m_stream->writeFixSize(&len, sizeof(len));
    if (ret != sizeof(len)) {
        SYLAR_LOG_WARN(g_logger) << "Failed to write message length";
        return false;
    }
    
    ret = m_stream->writeFixSize(json.c_str(), json.size());
    
    if (ret > 0 && m_controller->getMetrics()) {
        m_controller->getMetrics()->recordIOWrite(sizeof(len) + ret);
    }
    
    return ret == static_cast<int>(json.size());
}

void IcpSession::close() {
    bool expected = false;
    if (!m_closed.compare_exchange_strong(expected, true)) {
        return;  // 已经关闭
    }
    
    // 注销会话
    uint32_t car_id = m_carId.load(std::memory_order_acquire);
    if (m_registered && car_id != UINT32_MAX && m_onUnregister) {
        m_onUnregister(car_id);
        m_registered = false;
    }
    
    if (m_socket) {
        m_socket->cancelAll();  // 清理 epoll 事件，避免 IOManager 无法停止
        m_socket->close();
    }
    
    // 记录断开
    if (m_controller->getMetrics()) {
        m_controller->getMetrics()->recordConnectionClose();
    }
    
    SYLAR_LOG_INFO(g_logger) << "IcpSession closed, car_id=" << car_id;
}

bool IcpSession::isConnected() const {
    return !m_closed.load(std::memory_order_acquire) && 
           m_socket && m_socket->isConnected();
}

void IcpSession::handleLoop() {
    while (isConnected()) {
        if (!readMessage()) {
            break;
        }
    }
    
    close();
}

bool IcpSession::readMessage() {
    auto config = m_controller->getConfig();
    
    // 第一步: 读取消息头
    if (m_recvPos < MessageHeader::SIZE) {
        int need = MessageHeader::SIZE - m_recvPos;
        int ret = m_stream->read(m_recvBuffer.data() + m_recvPos, need);
        
        if (ret <= 0) {
            SYLAR_LOG_DEBUG(g_logger) << "Connection closed while reading header";
            return false;
        }
        
        m_recvPos += ret;
        
        if (m_controller->getMetrics()) {
            m_controller->getMetrics()->recordIORead(ret);
        }
        
        if (m_recvPos < MessageHeader::SIZE) {
            return true;  // 继续读取
        }
    }
    
    // 解析消息头
    MessageHeader header;
    auto parse_result = MessageParser::parseHeader(m_recvBuffer.data(), 
                                                    m_recvPos, header);
    
    if (parse_result != MessageParser::ParseResult::OK) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid message header";
        if (m_controller->getMetrics()) {
            m_controller->getMetrics()->recordIOError();
        }
        return false;
    }
    
    // 验证消息大小
    if (header.message_size > config->max_msg_size) {
        SYLAR_LOG_ERROR(g_logger) << "Message too large: " << header.message_size
                                   << " > " << config->max_msg_size;
        if (m_controller->getMetrics()) {
            m_controller->getMetrics()->recordIOError();
        }
        return false;
    }
    
    // 验证car_id
    if (header.car_id >= config->max_cars) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid car_id: " << header.car_id
                                   << " >= " << config->max_cars;
        // 读取并丢弃消息体
        size_t remaining = header.message_size - m_recvPos;
        while (remaining > 0) {
            int ret = m_stream->read(m_recvBuffer.data(), 
                                     std::min(remaining, m_recvBuffer.size()));
            if (ret <= 0) return false;
            remaining -= ret;
        }
        m_recvPos = 0;
        return true;
    }
    
    // Early Drop: 检查序列号
    RxSlot* slot = m_controller->getRxSlotManager()->getSlot(header.car_id);
    if (slot) {
        uint64_t current_seq = slot->getCurrentSeq();
        if (header.seq <= current_seq) {
            // 旧消息, 读取并丢弃
            SYLAR_LOG_DEBUG(g_logger) << "Stale message, dropping: seq=" 
                                       << header.seq << " current=" << current_seq;
            
            size_t remaining = header.message_size - m_recvPos;
            while (remaining > 0) {
                int ret = m_stream->read(m_recvBuffer.data(), 
                                         std::min(remaining, m_recvBuffer.size()));
                if (ret <= 0) return false;
                remaining -= ret;
                
                if (m_controller->getMetrics()) {
                    m_controller->getMetrics()->recordIORead(ret);
                }
            }
            
            m_recvPos = 0;
            m_controller->getMetrics()->recordDropStale(header.car_id);
            return true;
        }
    }
    
    // 第二步: 读取消息体
    while (m_recvPos < header.message_size) {
        int need = header.message_size - m_recvPos;
        int ret = m_stream->read(m_recvBuffer.data() + m_recvPos, need);
        
        if (ret <= 0) {
            SYLAR_LOG_DEBUG(g_logger) << "Connection closed while reading body";
            return false;
        }
        
        m_recvPos += ret;
        
        if (m_controller->getMetrics()) {
            m_controller->getMetrics()->recordIORead(ret);
        }
    }
    
    // 完整消息已接收, 处理消息
    processMessage(m_recvBuffer.data(), header.message_size);
    
    // 重置接收状态
    m_recvPos = 0;
    
    return true;
}

void IcpSession::processMessage(const uint8_t* data, size_t len) {
    // 解析消息头获取car_id和seq
    MessageHeader header;
    if (MessageParser::parseHeader(data, len, header) != 
        MessageParser::ParseResult::OK) {
        return;
    }
    
    // 首条消息 —— 注册 session 到 IcpServer
    if (!m_registered) {
        m_carId.store(header.car_id, std::memory_order_release);
        if (m_onRegister) {
            m_onRegister(header.car_id, shared_from_this());
        }
        m_registered = true;
        SYLAR_LOG_INFO(g_logger) << "IcpSession bound to car_id=" << header.car_id;
    }
    
    // 记录接收消息
    if (m_controller->getMetrics()) {
        m_controller->getMetrics()->recordRecvMsg(header.car_id);
    }
    
    // 写入RxSlot(覆盖旧数据)
    RxSlot* slot = m_controller->getRxSlotManager()->getSlot(header.car_id);
    if (!slot) {
        SYLAR_LOG_WARN(g_logger) << "No slot for car_id: " << header.car_id;
        return;
    }
    
    if (!slot->write(data, len, header.seq, header.timestamp_ms)) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to write to RxSlot, car_id=" 
                                   << header.car_id;
        return;
    }
    
    SYLAR_LOG_DEBUG(g_logger) << "Message written to RxSlot: car_id=" 
                               << header.car_id
                               << " seq=" << header.seq
                               << " size=" << len;
    
    // 通知控制层
    m_controller->notifyNewMessage(header.car_id, header.seq);
}

//------------------------------------------------------------------------------
// IcpServer
//------------------------------------------------------------------------------

IcpServer::IcpServer(IcpController::ptr controller,
                     IOManager* worker,
                     IOManager* accept_worker)
    : TcpServer(worker, accept_worker)
    , m_controller(controller) {
    
    setName("IcpServer");
    
    // 设置结果回调
    controller->setResultCallback([this](uint32_t car_id, const OutputMessage& msg) {
        onResult(car_id, msg);
    });
}

IcpServer::~IcpServer() {
    stop();
}

bool IcpServer::sendToClient(uint32_t car_id, const OutputMessage& msg) {
    IcpSession::ptr session;
    {
        RWMutex::ReadLock lock(m_sessionMutex);
        auto it = m_sessions.find(car_id);
        if (it == m_sessions.end()) {
            return false;
        }
        session = it->second;
    }
    
    if (session && session->isConnected()) {
        return session->sendResult(msg);
    }
    
    return false;
}

size_t IcpServer::getConnectionCount() const {
    RWMutex::ReadLock lock(m_sessionMutex);
    return m_sessions.size();
}

void IcpServer::handleClient(Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger) << "New client connection: " 
                              << client->getRemoteAddress()->toString();
    
    // 创建会话
    auto session = std::make_shared<IcpSession>(client, m_controller);
    
    // 设置注册/注销回调 —— 首条消息到达后自动注册
    session->setRegisterCallbacks(
        [this](uint32_t car_id, IcpSession::ptr s) {
            registerSession(car_id, s);
        },
        [this](uint32_t car_id) {
            unregisterSession(car_id);
        }
    );
    
    // 处理会话(阻塞直到连接关闭)
    {
        Mutex::Lock lock(m_socketMapMutex);
        m_socketToCarId[client.get()] = UINT32_MAX;  // 未知car_id
    }
    
    session->start();
    
    // 清理 socket 映射
    {
        Mutex::Lock lock(m_socketMapMutex);
        m_socketToCarId.erase(client.get());
    }
}

void IcpServer::registerSession(uint32_t car_id, IcpSession::ptr session) {
    RWMutex::WriteLock lock(m_sessionMutex);
    
    // 如果已有会话, 关闭旧会话
    auto it = m_sessions.find(car_id);
    if (it != m_sessions.end()) {
        it->second->close();
    }
    
    m_sessions[car_id] = session;
    
    SYLAR_LOG_DEBUG(g_logger) << "Session registered: car_id=" << car_id;
}

void IcpServer::unregisterSession(uint32_t car_id) {
    RWMutex::WriteLock lock(m_sessionMutex);
    m_sessions.erase(car_id);
    
    SYLAR_LOG_DEBUG(g_logger) << "Session unregistered: car_id=" << car_id;
}

void IcpServer::onResult(uint32_t car_id, const OutputMessage& msg) {
    if (!sendToClient(car_id, msg)) {
        SYLAR_LOG_WARN(g_logger) << "Failed to send result to car_id=" << car_id;
    }
}

//------------------------------------------------------------------------------
// IcpService
//------------------------------------------------------------------------------

IcpService::IcpService(IcpConfig::ptr config)
    : m_config(config) {
}

IcpService::~IcpService() {
    stop();
}

bool IcpService::init() {
    // 验证配置
    std::string error = m_config->validate();
    if (!error.empty()) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid config: " << error;
        return false;
    }
    
    // 创建IOManager (use_caller=false: 不占用调用方线程)
    m_ioManager = std::make_unique<IOManager>(
        m_config->io_threads, false, "icp_io");
    
    m_controlManager = std::make_unique<IOManager>(
        m_config->control_threads, false, "icp_ctrl");
    
    m_httpManager = std::make_unique<IOManager>(
        m_config->http_threads, false, "icp_http");
    
    // 创建控制器
    m_controller = std::make_shared<IcpController>(m_config);
    if (!m_controller->init(m_controlManager.get(), m_httpManager.get())) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to init controller";
        return false;
    }
    
    // 创建服务器
    m_server = std::make_shared<IcpServer>(
        m_controller, m_ioManager.get(), m_ioManager.get());
    
    // 绑定地址
    auto addr = Address::LookupAnyIPAddress(
        m_config->server.bind_address + ":" + 
        std::to_string(m_config->server.bind_port));
    
    if (!addr) {
        SYLAR_LOG_ERROR(g_logger) << "Invalid bind address: " 
                                   << m_config->server.bind_address << ":"
                                   << m_config->server.bind_port;
        return false;
    }
    
    if (!m_server->bind(addr)) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to bind: " << addr->toString();
        return false;
    }
    
    SYLAR_LOG_INFO(g_logger) << "IcpService initialized, bind=" << addr->toString();
    return true;
}

bool IcpService::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return true;
    }
    
    if (!m_server->start()) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to start server";
        return false;
    }
    
    m_running.store(true, std::memory_order_release);
    
    SYLAR_LOG_INFO(g_logger) << "IcpService started";
    return true;
}

void IcpService::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    
    m_running.store(false, std::memory_order_release);
    
    if (m_server) {
        m_server->stop();
    }
    
    if (m_controller) {
        m_controller->stop();
    }
    
    // 停止IOManager
    if (m_ioManager) {
        m_ioManager->stop();
    }
    if (m_controlManager) {
        m_controlManager->stop();
    }
    if (m_httpManager) {
        m_httpManager->stop();
    }
    
    SYLAR_LOG_INFO(g_logger) << "IcpService stopped";
}

IcpMetrics::ptr IcpService::getMetrics() {
    if (m_controller) {
        return m_controller->getMetrics();
    }
    return nullptr;
}

} // namespace icp
} // namespace sherry
