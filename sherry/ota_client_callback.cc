#include "ota_client_callback.h"

#include "sherry.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sherry{

void OTAClientCallbackManager::regist_callback(const std::string& topic, CallbackFunc callback){
    RWMutexType::WriteLock lock(m_mutex);
    m_callbacks[topic] = std::move(callback);
    SYLAR_LOG_INFO(g_logger) << "regist topic:" << topic
                             << " sucsess.";
}

void OTAClientCallbackManager::on_message(const std::string& topic, const std::string& payload){
    CallbackFunc cb = nullptr;
    
    {
        RWMutexType::ReadLock lock(m_mutex);

        if(m_callbacks.find(topic) != m_callbacks.end()){
            cb = m_callbacks[topic];
        }
    }

    if(cb){
        try{
            cb(topic, payload);
        } catch(const std::exception& e){
            SYLAR_LOG_ERROR(g_logger) << "Exception in MQTT callback for topic [" << topic
                                      << "]: " << e.what();
        }
    } else {
        SYLAR_LOG_WARN(g_logger) << "No callback found for MQTT topic: " << topic;
    }
}
}