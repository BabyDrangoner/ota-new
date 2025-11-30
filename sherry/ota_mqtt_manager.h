#ifndef _SHERRY_OTAMqttManager_H__
#define _SHERRY_OTAMqttManager_H__

#include "sherry.h"
#include "scheduler.h"
#include "ota_notifier.h"
#include "ota_query_responder.h"
#include "mqtt_client.h"
#include "timer.h"
#include "http/http.h"
#include "http/http_session.h"
#include "ota_client_callback.h"
#include "ota_http_response_builder.h"
#include "ota_subscribe_download.h"
#include "iomanager.h"
#include "thread.h"

#include <unordered_map>
#include <unordered_set>

namespace sherry{
class OTACommandDispatcher;
class OTAMqttManager{
public:
    typedef std::shared_ptr<OTAMqttManager> ptr;
    typedef RWMutex RWMutexType;
    OTAMqttManager(size_t file_size, const std::string& protocol
               , const std::string& host, int port
               , const std::string& redis_pool_name = ""
               , const std::string& redis_mq_pool_name = "");

    static OTAMqttManager* GetThis();
    void SetThis();
    
    bool is_stopped() const { return m_stopped;}
    bool is_running() const { return m_running;}

    void set_protocol(const std::string& v){ m_protocol = v;}
    void set_host(const std::string& v){ m_host = v;}
    void set_port(int v) { m_port = v;}
    
    std::string get_protocol() const{ return m_protocol;}
    std::string get_host() const { return m_host;}
    int get_port() const { return m_port;}
    uint16_t get_device_types_counts() const { return m_device_types_counts;}
    uint64_t get_device_counts() const { return m_device_counts;}

    bool add_device(uint16_t device_type, uint32_t device_no);
    bool remove_device(uint16_t device_type, uint32_t device_no);

    void ota_notify(uint16_t device_type
                    , const std::string& name
                    , const std::string& version);
    void ota_stop_notify(uint16_t device_type
                        , const std::string& name
                        , const std::string& version);
    bool check_device(uint16_t device_type, uint32_t device_no);
    bool check_device(uint16_t device_type);

private:
    void redis_message_queue_thread_run();
    void redis_message_queue_thread_command_dispatch(const std::string& command);
private:
    RWMutexType m_mutex;
    RWMutexType m_notifier_mutex;
    std::string m_protocol;
    std::string m_host;
    int m_port;

    bool m_running;
    bool m_stopped;

    ssize_t m_buffer_size;

    IOManager::ptr m_timer_mgr;
    uint16_t m_device_types_counts;
    uint64_t m_device_counts;
    OTAClientCallbackManager::ptr m_callback_mgr;
    MqttClientManager::ptr m_client_mgr;
    
    std::unordered_map<uint16_t, std::unordered_set<uint32_t>> m_device_type_nums;
    std::unordered_map<std::string, OTANotifier::ptr> m_ota_notifier_map;
    std::unordered_map<uint16_t, std::unordered_map<uint32_t, OTASubscribeDownload::ptr>> m_ota_subscribe_download_map;
    
    std::string m_redis_pool_name;
    std::string m_redis_mq_pool_name;
    
    std::unique_ptr<Thread> m_redis_message_queue_consume_thread;
    std::shared_ptr<OTACommandDispatcher> m_command_dispatcher;
    IOManager::ptr m_command_ioMgr; 
};
}
#endif