#ifndef _SHERRY_DEVICE_COMMUNICATION_H__
#define _SHERRY_DEVICE_COMMUNICATION_H__

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include "sherry/mqtt_client.h"
#include "sherry/socket.h"
#include "sherry/http/http_connection.h"
#include "sherry/iomanager.h"

namespace sherry{
namespace device{

enum class NET_ERROR_CODE {
    SUCCESS = 0,
    FAILED = 1,
    NOT_CONNECTED = 2,
    TIMEOUT = 3,
    INVALID_PARAM = 4,
    RETRY_EXHAUSTED = 5
};

struct topicInfo{
    std::string pub_topic{""};
    std::string sub_topic{""};
};

struct sendCtx{
    const char* msg;
    const size_t msg_size;
    const size_t retry_cnt;
    std::function<void(NET_ERROR_CODE, struct sendCtx*)> complete_cb;

    sendCtx(const char* _msg, const size_t _msg_size, const size_t _retry_cnt)
           :msg(_msg), msg_size(_msg_size), retry_cnt(_retry_cnt){}
};

struct recvCtx{
    const char* buf;
    const size_t buf_size;
    std::function<void(NET_ERROR_CODE, recvCtx*)> complete_cb;

    recvCtx(const char* _buf, const size_t _buf_size)
    : buf(_buf), buf_size(_buf_size){}
};

struct pubCtx{
    const char* msg;
    const size_t msg_size;
    const struct topicInfo* topics;
    const struct subCtx* sub_ctx;
    std::function<void(NET_ERROR_CODE, struct pubCtx*)> complete_cb;

    pubCtx(const char* _msg, const size_t _msg_size,
           const struct topicInfo* _topics,
           const struct subCtx* _sub_ctx)
           : msg(_msg), msg_size(_msg_size),
           topics(_topics), sub_ctx(_sub_ctx){}
};

struct subCtx{
    const char* buf;
    const size_t buf_size;
    const struct topicInfo* topics;
    const struct pubCtx* pub_ctx;
    std::function<void(NET_ERROR_CODE, struct subCtx*)> complete_cb;

    subCtx(const char* _buf, const size_t _buf_size,
           const struct topicInfo* _topics,
           const struct pubCtx* _pub_ctx)
           : buf(_buf), buf_size(_buf_size),
           topics(_topics), pub_ctx(_pub_ctx){}
};

class DeviceCommnicator{
public:
    DeviceCommnicator(const std::string& ip, const int port);

    virtual ~DeviceCommnicator();

    virtual void connect() = 0;
    virtual void disconnect() = 0;

    // http
    virtual void send(struct sendCtx& ctx){};
    virtual void recv(struct recvCtx& ctx){};   

    // mqtt
    virtual void pub(struct pubCtx& ctx){};
    virtual void sub(struct subCtx& ctx){};

    bool is_connected() { return m_is_connected;}

protected:
    bool m_is_connected;
    std::string m_ip;
    int m_port;
};

class MqttDeviceCommunicator : public DeviceCommnicator{
public:
    typedef std::shared_ptr<MqttDeviceCommunicator> ptr;
    
    MqttDeviceCommunicator(const std::string& serv_ip, const int serv_port);
    ~MqttDeviceCommunicator();

    virtual void connect();
    virtual void disconnect() override;
    
    virtual void pub(struct pubCtx& ctx) override;
    virtual void sub(struct subCtx& ctx) override;

private:
    std::vector<topicInfo*> m_topics;
    MqttClient::ptr m_client;
};

class HtppDeviceCommunicator : public DeviceCommnicator{
public:
    typedef std::shared_ptr<HtppDeviceCommunicator> ptr;
    HtppDeviceCommunicator(const std::string serv_ip, int serv_port, IOManager::ptr io_mgr);

    virtual void connect();
    virtual void disconnect() override;

    virtual void send(struct sendCtx& ctx) override;
    virtual void recv(struct recvCtx& ctx) override;
private:
    Socket::ptr m_sock;
    http::HttpConnection::ptr m_connection;
    IOManager::ptr m_io_mgr;
};

class SocketDeviceCommunicator : public DeviceCommnicator {
public:
    typedef std::shared_ptr<SocketDeviceCommunicator> ptr;

    SocketDeviceCommunicator(const std::string& serv_ip,
                             int serv_port,
                             IOManager::ptr io_mgr = nullptr,
                             uint64_t connect_timeout_ms = 5000);

    virtual void connect() override;
    virtual void disconnect() override;

    virtual void send(struct sendCtx& ctx) override;
    virtual void recv(struct recvCtx& ctx) override;

private:
    int writeFixSize(const void* buffer, size_t length);
    int readFixSize(void* buffer, size_t length);

private:
    Socket::ptr m_sock;
    IOManager::ptr m_io_mgr;
    uint64_t m_connect_timeout_ms;
};

} // namespace device

} // namespace sherry



#endif