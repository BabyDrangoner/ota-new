#ifndef __SHERRY_OTA_DEVICE_
#define __SHERRY_OTA_DEVICE_


#include "../mqtt_client.h"
#include "../ota_notifier.h"

#include <vector>
#include <string>

namespace sherry{

class OTADevice{
public:
    typedef std::shared_ptr<OTADevice> ptr;
    OTADevice(uint16_t device_type, int port
              , const std::string& protocol
              , const std::string& host
              , OTAClientCallbackManager::ptr cb_mgr
              , std::vector<std::string>& sub_topics
              , std::vector<int>& sub_qos
              , const std::vector<std::string>& redis_keys
              , const std::vector<int> redis_expire_seconds
              , const std::string& redis_pool_name);
    void start();
    void stop();
    
    bool is_running() const { return m_is_running;}

    int ota_notify(const std::string& name
                   , const std::string& version
                   , IOManager::ptr timer_mgr
                   , const std::string& redis_pool_name);
    int ota_stop_notify(const std::string& name
                        , const std::string& version
                        , const std::string& redis_pool_name);
    
private:
    uint16_t m_device_type;
    bool m_is_running;
    MqttClient::ptr m_client;
    OTAClientCallbackManager::ptr m_callback_mgr;
    std::vector<std::string> m_sub_topics;
    std::vector<int> m_sub_qos;
    OTANotifier::ptr m_notifier;
};

} // namespace sherry

#endif