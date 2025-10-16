#ifndef __SHERRY_OTA_QUERY_RESPONDER_H__
#define __SHERRY_OTA_QUERY_RESPONDER_H__

#include "mqtt_client.h"
#include "ota_version_message.h"
#include <string>
#include <memory>

namespace sherry{

class OTAQueryResponder : public std::enable_shared_from_this<OTAQueryResponder>{
public:
    typedef std::shared_ptr<OTAQueryResponder> ptr;
    typedef RWMutex RWMutexType;

    OTAQueryResponder(int device_type, int device_no, MqttClientManager::ptr cli_mgr);
    
    std::string format_payload(const std::string& action);
    void publish_query(const std::string& action, int qos = 1, bool retain = false);
    void subscribe_responder(const std::string& pub_topic, const std::string& sub_topic, int qos);
    void subscribe_on_success(const std::string& pub_topic, const std::string& sub_topic);
private:
    RWMutexType m_mutex;
    int m_device_type;
    int m_device_no;
    MqttClientManager::ptr m_client_manager;

    MqttClient::ptr m_client;

    
};

}
#endif