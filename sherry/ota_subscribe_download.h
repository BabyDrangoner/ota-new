#ifndef _SHERRY_OTA_DOWNLOAD_H__
#define _SHERRY_OTA_DOWNLOAD_H__

#include "sherry.h"
#include "mqtt_client.h"

namespace sherry{

class OTASubscribeDownload{
public:
    typedef std::shared_ptr<OTASubscribeDownload> ptr;
    typedef RWMutex RWMutexType;

    OTASubscribeDownload(const std::string& topic
                        ,int device_type
                        ,int device_no
                        ,MqttClientManager::ptr cli_mgr
                        ,OTAClientCallbackManager::ptr cb_mgr);

    void subscribe_download(const std::string& topic, int qos=1);
    
private:
    RWMutexType m_mutex;
    std::string m_topic;
    int m_device_type;
    int m_device_no;
    MqttClientManager::ptr m_client_manager;
    OTAClientCallbackManager::ptr m_callback_manager;
    MqttClient::ptr m_client;

};

}
#endif