#ifndef __SHERRY_ICP_SERVER_H__
#define __SHERRY_ICP_SERVER_H__

#include <memory>
#include <unordered_map>

#include "icp_config.h"
#include "icp_controller.h"
#include "icp_protocol.h"
#include "sherry/tcp_server.h"
#include "sherry/socket_stream.h"
#include "sherry/thread.h"

namespace sherry {
namespace icp {

/**
 * @brief ICP 客户端会话
 * 
 * 管理单个设备连接:
 * - 读取socket数据
 * - 帧解析(按message_size拼包)
 * - 将完整消息写入RxSlot
 * - 通知控制层
 */
class IcpSession : public std::enable_shared_from_this<IcpSession> {
public:
    typedef std::shared_ptr<IcpSession> ptr;
    
    /// 注册/注销回调类型
    using RegisterCb   = std::function<void(uint32_t, IcpSession::ptr)>;
    using UnregisterCb = std::function<void(uint32_t)>;
    
    /**
     * @brief 构造函数  
     * @param socket 客户端socket
     * @param controller ICP控制器
     */
    IcpSession(Socket::ptr socket, IcpController::ptr controller);
    
    ~IcpSession();
    
    /**
     * @brief 设置注册/注销回调（由 IcpServer 在 start 前调用）
     */
    void setRegisterCallbacks(RegisterCb on_register, UnregisterCb on_unregister);
    
    /**
     * @brief 开始处理
     */
    void start();
    
    /**
     * @brief 发送结果消息
     */
    bool sendResult(const OutputMessage& msg);
    
    /**
     * @brief 关闭会话
     */
    void close();
    
    /**
     * @brief 获取socket
     */
    Socket::ptr getSocket() const { return m_socket; }
    
    /**
     * @brief 检查是否连接
     */
    bool isConnected() const;
    
    /**
     * @brief 获取关联的 car_id (UINT32_MAX 表示尚未知)
     */
    uint32_t getCarId() const { return m_carId.load(std::memory_order_acquire); }
    
private:
    /**
     * @brief 主处理循环
     */
    void handleLoop();
    
    /**
     * @brief 读取完整消息
     * @return true表示成功读取, false表示连接关闭或错误
     */
    bool readMessage();
    
    /**
     * @brief 处理接收到的消息
     */
    void processMessage(const uint8_t* data, size_t len);
    
private:
    Socket::ptr m_socket;
    SocketStream::ptr m_stream;
    IcpController::ptr m_controller;
    
    // 接收缓冲区
    std::vector<uint8_t> m_recvBuffer;
    size_t m_recvPos;
    
    // 状态
    std::atomic<bool> m_closed{false};
    std::atomic<uint32_t> m_carId{UINT32_MAX};  // 首条消息后更新
    bool m_registered{false};                    // 是否已注册到 Server
    
    // 注册/注销回调
    RegisterCb   m_onRegister;
    UnregisterCb m_onUnregister;
    
    // 写锁(发送需要串行化)
    Mutex m_writeMutex;
};

/**
 * @brief ICP TCP 服务器
 * 
 * 职责:
 * - epoll接入
 * - 管理客户端连接
 * - 创建IcpSession处理每个连接
 */
class IcpServer : public TcpServer {
public:
    typedef std::shared_ptr<IcpServer> ptr;
    
    /**
     * @brief 构造函数
     * @param controller ICP控制器
     * @param worker IO工作线程
     * @param accept_worker 接受连接的线程
     */
    IcpServer(IcpController::ptr controller,
              IOManager* worker = IOManager::GetThis(),
              IOManager* accept_worker = IOManager::GetThis());
    
    ~IcpServer();
    
    /**
     * @brief 向指定车辆发送结果
     */
    bool sendToClient(uint32_t car_id, const OutputMessage& msg);
    
    /**
     * @brief 获取连接数
     */
    size_t getConnectionCount() const;
    
protected:
    /**
     * @brief 处理新客户端连接
     */
    void handleClient(Socket::ptr client) override;
    
private:
    /**
     * @brief 注册会话
     */
    void registerSession(uint32_t car_id, IcpSession::ptr session);
    
    /**
     * @brief 注销会话
     */
    void unregisterSession(uint32_t car_id);
    
    /**
     * @brief 结果回调处理
     */
    void onResult(uint32_t car_id, const OutputMessage& msg);
    
private:
    IcpController::ptr m_controller;
    
    // 会话管理
    mutable RWMutex m_sessionMutex;
    std::unordered_map<uint32_t, IcpSession::ptr> m_sessions;
    
    // Socket到car_id的映射
    mutable Mutex m_socketMapMutex;
    std::unordered_map<Socket*, uint32_t> m_socketToCarId;
};

/**
 * @brief ICP 服务管理器
 * 
 * 顶层接口, 管理整个ICP系统:
 * - 创建和管理所有组件
 * - 提供统一的启动/停止接口
 */
class IcpService {
public:
    typedef std::shared_ptr<IcpService> ptr;
    
    /**
     * @brief 构造函数
     * @param config ICP配置
     */
    explicit IcpService(IcpConfig::ptr config);
    
    ~IcpService();
    
    /**
     * @brief 初始化服务
     * @return true表示成功
     */
    bool init();
    
    /**
     * @brief 启动服务
     * @return true表示成功
     */
    bool start();
    
    /**
     * @brief 停止服务
     */
    void stop();
    
    /**
     * @brief 获取控制器
     */
    IcpController::ptr getController() { return m_controller; }
    
    /**
     * @brief 获取服务器
     */
    IcpServer::ptr getServer() { return m_server; }
    
    /**
     * @brief 获取指标
     */
    IcpMetrics::ptr getMetrics();
    
    /**
     * @brief 检查是否运行中
     */
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    
private:
    IcpConfig::ptr m_config;
    
    // 线程池/调度器
    std::unique_ptr<IOManager> m_ioManager;      // IO线程
    std::unique_ptr<IOManager> m_controlManager; // 控制线程
    std::unique_ptr<IOManager> m_httpManager;    // HTTP线程
    
    // 核心组件
    IcpController::ptr m_controller;
    IcpServer::ptr m_server;
    
    std::atomic<bool> m_running{false};
};

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_SERVER_H__
