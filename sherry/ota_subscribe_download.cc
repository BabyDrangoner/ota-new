#include "ota_subscribe_download.h"
#include "log.h"
#include <functional>

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sherry{

OTASubscribeDownload::OTASubscribeDownload(const std::string& topic
                                          ,int device_type
                                          ,int device_no
                                          ,MqttClientManager::ptr cli_mgr
                                          ,OTAClientCallbackManager::ptr cb_mgr)
    :m_topic(topic)
    ,m_device_type(device_type)
    ,m_device_no(device_no)
    ,m_client_manager(cli_mgr)
    ,m_callback_manager(cb_mgr){
    m_client = cli_mgr->get_client(m_device_type);
}

void OTASubscribeDownload::subscribe_download(const std::string& topic, int qos){
    std::function<void(const std::string& topic, int res)> cb = [this, qos](const std::string& topic, int res){
        if(res == -1){
            m_client->subscribe(topic, 1);
            SYLAR_LOG_ERROR(g_logger) << "subscribe to topic: " << topic
                                    << " for downloading details failed, now subscribe again!";
        } else if(res == 0){
            SYLAR_LOG_INFO(g_logger) << "subscribe to topic: " << topic
                                    << " for downloading details success!";
        }
    };

    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_client){
            if(!m_client->get_isconnected()){
                m_client->connect(true);
            }
            if(!m_client->get_isconnected()){
                SYLAR_LOG_ERROR(g_logger) << "client can not connect to mqtt server, when subcribing to topic: " << topic 
                                          << "for download details.";
                return;
            }

            m_client->subscribe(topic, qos, cb);
            return;
        }
    }

    {
        RWMutexType::WriteLock lock(m_mutex);
        if(!m_client_manager){
            SYLAR_LOG_ERROR(g_logger) << "client manager is null, when creating device type: " << m_device_type
                                      << " client, when subscribing to topic: " << topic 
                                      << " for download details";
            return;
        }

        m_client = m_client_manager->get_client(m_device_type);
        if(!m_client->get_isconnected()){
            m_client->connect(true);
        }

        if(!m_client->get_isconnected()){
            SYLAR_LOG_ERROR(g_logger) << "client manager is null, when connecting to mqtt server"
                                      << ", when subscribing to topic: " << topic 
                                      << " for download details";
            return;
        }

        m_client->subscribe(topic, qos, cb);

    }
}

}