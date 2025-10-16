#ifndef __SHERRY_MQTTCLIENT_H__
#define __SHERRY_MQTTCLIENT_H__

#include <mqtt/async_client.h>
#include "address.h"
#include "sherry.h"
#include "mqtt_listener.h"
#include "ota_client_callback.h"

#include <functional>

namespace sherry {

class MqttClient : public virtual mqtt::callback {
public:
    typedef std::shared_ptr<MqttClient> ptr;
    typedef RWMutex RWMutexType;
    typedef std::function<void(const std::string&, int)> SubPubCallback;
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    MqttClient(const std::string& protocol, int port, const std::string& host,
               const std::string& client_id, int m_device_type,
               OTAClientCallbackManager::ptr = nullptr);
    ~MqttClient() = default;

    void CreateMqttClient(mqtt::async_client*);

    int get_port() const { return m_port; }
    int get_device_type() const { return m_device_type; }
    bool get_isconnected() const { return m_isconnected; }
    mqtt::connect_options get_connOpts() const { return m_connOpts; }
    MqttAddress::ptr get_serverAddress() const { return m_serverAddress; }
    mqtt::async_client* get_client() const { return m_client; }

    void set_port(int v);
    void set_serverAddr(const std::string& protocol, int port, const std::string& host);
    void set_connOpts(int KAInterval, bool AutoRconn, bool CleanSesstion,
                      const std::string& UserName, const std::string& PassWord,
                      int ConnTimeout);

    void set_device_type(int v) { m_device_type = v; }
    void set_isconnected(bool v) { m_isconnected = v; }
    void set_cbmgr(OTAClientCallbackManager::ptr v) { m_cbmgr = v; }

    void connect(bool use_opts = false);
    void subscribe(const std::string& topic, int qos = 1,
                   std::function<void(const std::string&, int)> callback = nullptr);
    void publish(const std::string& topic, const std::string payload, int qos = 1,
                 bool retained = false, std::function<void(const std::string&, int)> callback = nullptr);
    void disconnect(std::function<void(int)> callback = nullptr, int timeout = -1);
    void unsubscribe(const std::string& topic,
                     std::function<void(const std::string&, int)> callback = nullptr);

    // mqtt::callback 接口
    void connected(const std::string& cause) override;
    void connection_lost(const std::string& cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;
    void delivery_complete(mqtt::delivery_token_ptr tok) override;

private:
    RWMutexType m_mutex;
    int m_port;
    int m_device_type;
    bool m_isconnected;
    std::string m_protocol;
    std::string m_host;
    std::string m_client_id;
    MqttAddress::ptr m_serverAddress;
    mqtt::async_client* m_client;
    mqtt::connect_options m_connOpts;
    
    // 业务层注册的消息处理回调
    OTAClientCallbackManager::ptr m_cbmgr;

    sherry::MqttActionListener m_listener;

};


class MqttClientManager{
public:
    typedef std::shared_ptr<MqttClientManager> ptr;
    typedef RWMutex RWMutexType;

    MqttClientManager(int port, const std::string& protocol, const std::string& host, OTAClientCallbackManager::ptr cb_mgr);

    MqttClient::ptr get_client(int device_type);
    void delete_client(int device_type);


    void set_protocol(const std::string& v){
        RWMutexType::WriteLock lock(m_mutex);
        m_protocol = v;
    }

    void set_port(int v){
        RWMutexType::WriteLock lock(m_mutex);
        m_port = v;
    }

    void set_host(const std::string& v){
        RWMutexType::WriteLock lock(m_mutex);
        m_host = v;
    }
    
    std::string get_protocol(){
        RWMutexType::ReadLock lock(m_mutex);
        return m_protocol;
    }

    int get_port(){
        RWMutexType::ReadLock lock(m_mutex);
        return m_port;
    }

    std::string get_host(){
        RWMutexType::ReadLock lock(m_mutex);
        return m_host;
    }

    int get_clientNum(){
        RWMutexType::ReadLock lock(m_mutex);
        return m_client_num;
    }

private:
    RWMutexType m_mutex;
    int m_port;
    std::string m_protocol;
    std::string m_host;
    OTAClientCallbackManager::ptr m_cbMgr;
    std::unordered_map<int, MqttClient::ptr> m_clients;

    int m_client_num = 0;
    int m_client_id = 0;

};

}  // namespace sherry

#endif // __SHERRY_MQTTCLIENT_H__
