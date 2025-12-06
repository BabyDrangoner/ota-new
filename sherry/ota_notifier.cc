#include "ota_notifier.h"
#include "sherry.h"
#include "../include/json/json.hpp"

namespace sherry{

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

std::string OTAMessage::to_json() const {
    nlohmann::json j = {
        {"name", name},
        {"version", version},
        {"file_name", file_name},
        {"file_size", file_size},
        {"url_path", url_path},
        {"md5_value", md5_value},
        {"launch_mode", launch_mode},
        {"upgrade_mode", upgrade_mode},
        {"time", time}
    };
    return j.dump();
}

OTANotifier::OTANotifier(IOManager::ptr timer_mgr
                        ,const std::string& topic
                        ,MqttClient::ptr client
                        ,uint64_t interval_ms)
    :m_timer_mgr(timer_mgr)
    ,m_topic(std::move(topic))
    ,m_client(client)
    ,m_interval_ms(interval_ms){
}

void OTANotifier::set_message(const OTAMessage& msg){
    RWMutexType::WriteLock lock(m_mutex);
    m_msg = std::move(msg);
}

void OTANotifier::start(){
    SYLAR_ASSERT(m_timer_mgr);

    RWMutexType::WriteLock lock(m_mutex);
    m_timer = m_timer_mgr->addTimer(m_interval_ms, [this](){
        this->publish_once();
    }, true);

}

void OTANotifier::stop(){
    RWMutexType::WriteLock lock(m_mutex);
    if(m_timer){
        m_timer->cancel();
        m_timer = nullptr;
    }
}

void OTANotifier::publish_once(){
    if(!m_client){
        SYLAR_LOG_WARN(g_logger) << "MQTT client is null.";
        return;
    }
    if(m_client && !m_client->get_isconnected()){
        SYLAR_LOG_ERROR(g_logger) << "MQTT client is disconnected.";
        m_client->connect(true);
    }
    
    RWMutexType::ReadLock lock(m_mutex);
    std::string payload = m_msg.to_json();
    lock.unlock();
    m_client->publish(m_topic, payload, 1, true);
    
}

bool OTANotifier::get_notify_message(uint16_t device_type, const std::string& name
                                     , const std::string& version, struct OTAMessage& msg){
    msg.name = name;
    msg.version = version;
    msg.time = getCurrentTimeString();
    msg.file_name = "agsspds_20241110.zip";
    msg.file_size = 6773120;
    msg.url_path = "http://127.0.0.1:18882/download/ota/agsspds";
    msg.md5_value = "ed076287532e86365e841e92bfc50d8c";
    msg.launch_mode = 0;
    msg.upgrade_mode = 1;

    return true;

}

}