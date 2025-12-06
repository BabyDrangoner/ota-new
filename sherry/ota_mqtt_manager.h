#ifndef _SHERRY_OTAMqttManager_H__
#define _SHERRY_OTAMqttManager_H__

#include "sherry.h"
#include "config.h"
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
#include "ota/ota_device.h"
#include "ota/ota_servlet.h"

#include <unordered_map>
#include <unordered_set>

namespace sherry{
class OTACommandDispatcher;
class OTAMqttManager{
public:
    typedef std::shared_ptr<OTAMqttManager> ptr;
    typedef RWMutex RWMutexType;
    OTAMqttManager(size_t file_size
               , OTAClientCallbackManager::ptr cb_mgr
               , const std::string& redis_pool_name = ""
               , const std::string& redis_mq_pool_name = "");

    static OTAMqttManager* GetThis();
    void SetThis();
    
    bool is_running() const { return m_running;}
    bool is_stopped() const { return !m_running;}

    uint64_t get_device_counts() const { return m_device_counts;}

    void start();
    void stop();

    bool add_device(uint16_t device_type
                    , const std::string& protocol
                    , const std::string& host
                    , int port
                    , OTAClientCallbackManager::ptr cb_mgr
                    , const std::vector<std::string>& sub_topics
                    , const std::vector<int>& sub_qos
                    , const std::vector<std::string>& redis_keys
                    , const std::vector<int> redis_expire_seconds);
    bool remove_device(uint16_t device_type);

    int ota_notify(uint16_t device_type
                    , const std::string& name
                    , const std::string& version);
    int ota_stop_notify(uint16_t device_type
                        , const std::string& name
                        , const std::string& version);
    bool check_device(uint16_t device_type);

private:
    void redis_message_queue_thread_run();
    void redis_message_queue_thread_command_dispatch(const std::string& command);
private:
    bool m_running;
    ssize_t m_buffer_size;
    uint64_t m_device_counts;

    OTAClientCallbackManager::ptr m_callback_mgr;
    IOManager::ptr m_timer_mgr;
    
    RWMutexType m_device_mutex;
    std::unordered_map<uint16_t, OTADevice::ptr> m_devices;
    
    std::string m_redis_pool_name;
    std::string m_redis_mq_pool_name;
    
    std::unique_ptr<Thread> m_redis_message_queue_consume_thread;
    std::shared_ptr<OTACommandDispatcher> m_command_dispatcher;
    IOManager::ptr m_command_ioMgr; 
};
}
#endif