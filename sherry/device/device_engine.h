#ifndef _SHERRY_DEVICE_ENGINE_H__
#define _SHERRY_DEVICE_ENGINE_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "sherry/iomanager.h"
#include "sherry/socket.h"
#include "sherry/socket_stream.h"
#include "sherry/thread.h"

#include "sherry/device/device_camera.h"
#include "sherry/icp/icp_protocol.h"

namespace sherry{
namespace device{

class DeviceEngine : public std::enable_shared_from_this<DeviceEngine> {
public:
    using ptr = std::shared_ptr<DeviceEngine>;

    enum class Transport {
        SocketTcp = 0,
        Mqtt = 1,
    };

    enum class MsgType : uint16_t {
        Image = 1,
        Text = 2,
    };

    struct Options {
        std::string server_ip{"127.0.0.1"};
        int server_port{0};
        uint64_t connect_timeout_ms{5000};
        uint64_t send_interval_ms{1000};
        Transport listen_transport{Transport::SocketTcp};
        size_t max_payload_bytes{16 * 1024 * 1024};
        uint64_t car_id{0};  // 车辆ID
    };

    DeviceEngine(Options opt, Camera::ptr camera, IOManager::ptr io_mgr = nullptr);
    ~DeviceEngine();

    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    void setServerMessageCallback(std::function<void(const std::string&)> cb);
    
    /**
     * @brief 设置 ICP 结果回调
     * @param cb 接收到 ICP 输出消息时的回调函数
     */
    void setIcpResultCallback(std::function<void(const icp::OutputMessage&)> cb);

private:
    bool connectSocket();
    
    /**
     * @brief 构建 ICP 协议格式的消息
     * @return 构建好的消息数据
     */
    std::vector<uint8_t> buildIcpMessage();
    void closeSocket();

    void sendLoop();
    void recvLoop();

    int writeFixSize(const void* buffer, size_t length);
    int readFixSize(void* buffer, size_t length);

private:
    Options m_opt;
    Camera::ptr m_camera;
    IOManager::ptr m_io_mgr;

    Socket::ptr m_sock;
    SocketStream::ptr m_stream;

    Thread::ptr m_send_thread;
    Thread::ptr m_recv_thread;

    std::atomic<bool> m_running{false};
    std::function<void(const std::string&)> m_on_server_msg;
    std::function<void(const icp::OutputMessage&)> m_on_icp_result;

    uint64_t m_car_id{0};              // 车辆ID
    std::atomic<uint64_t> m_seq{0};    // 单车递增序列号
};

// Backward-compat alias (old typo name); currently unused.
using DevcieEngine = DeviceEngine;
} // namespace device 
} // namespace sherry

#endif