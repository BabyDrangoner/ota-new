#include "ota_device.h"
#include "../log.h"
#include "../db/redis_util.h"
#include "../db/redis.h"
#include "ota_hash.h"
#include "../hash.h"
#include <memory>

#define TAG "OTADevice"

namespace sherry{

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
OTADevice::OTADevice(uint16_t device_type, int port
                     , const std::string& protocol
                     , const std::string& host
                     , OTAClientCallbackManager::ptr cb_mgr
                     , std::vector<std::string>& sub_topics
                     , std::vector<int>& sub_qos
                     , const std::vector<std::string>& redis_keys
                     , const std::vector<int> redis_expire_seconds
                     , const std::string& redis_pool_name)
    :m_device_type(device_type)
    ,m_is_running(false)
    ,m_client(
        std::make_shared<MqttClient>(protocol, port, host
                                     , std::to_string(device_type), cb_mgr))
    ,m_sub_topics(std::move(sub_topics))
    ,m_sub_qos(std::move(sub_qos)){
    for(int i = 0;i < (int)m_sub_topics.size();++i){
        auto redis_key = redis_keys[i];
        auto redis_expire_second = redis_expire_seconds[i];
        cb_mgr->regist_callback(m_sub_topics[i]
            , [redis_pool_name, redis_key, redis_expire_second]
            (const std::string& topic, const std::string& payload){
            SYLAR_LOG_INFO(g_logger) << TAG
                << "Subscribe topic " << topic
                << " payload " << payload;
            if(redis_safe_set_key_value(redis_pool_name, redis_key
                                        , payload, redis_expire_second) == 0){
                SYLAR_LOG_ERROR(g_logger) << TAG
                    << "Subscribe topic " << topic
                    << "set redis " << redis_pool_name 
                    << " value failed.";
            }
        });
    }
}

void OTADevice::start(){
    m_client->connect(true);
    if(!m_client->get_isconnected()){
        SYLAR_LOG_ERROR(g_logger) << TAG
            << "mqtt client connect fail";
        return;
    }

    std::function<void(const std::string &, int)> subscribe_callback =
        [](const std::string& topic, int code) {
            if(code == -1){
                SYLAR_LOG_ERROR(g_logger) << TAG
                    << "subscribe to topic : " << topic
                    << " fail.";
            } else {
                SYLAR_LOG_INFO(g_logger) << TAG 
                    << "subscribe to topic : " << topic
                    << " success.";
            }
    };

    for(int i = 0;i < (int)m_sub_topics.size();++i){
        m_client->subscribe(m_sub_topics[i], m_sub_qos[i]
                            , subscribe_callback);
    }
    m_is_running = true;
}

void OTADevice::stop(){
    m_client->disconnect();
    m_is_running = false;
}

int OTADevice::ota_notify(const std::string& name
                          , const std::string& version
                          , IOManager::ptr timer_mgr
                          , const std::string& redis_pool_name){
    struct OTAMessage msg;
    if(!OTANotifier::get_notify_message(m_device_type, name, version, msg)){
        
        std::stringstream ss;
        ss << TAG
            << "device_type = " << m_device_type
            << ", name = " << name
            << ", version = " << version
            << " get notify message error";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        return 0;
    }
    std::stringstream ss;
    ss << "/ota/" << m_device_type
       << "/" << name 
       << "/notify";
    
    std::string topic(ss.str());

    // 检查 Redis 中是否已有版本记录（reply->str 不为 NULL 且不为空字符串）
    const std::string redis_device_group_key{OTAHash::get_device_group_mudule_hash("notify", m_device_type, name)};
    auto reply = RedisUtil::Cmd(redis_pool_name, "GET %s", redis_device_group_key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << m_device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        return 0;
    }
    if(reply->type == REDIS_REPLY_STRING && reply->str && reply->str[0] != '\0'){
        std::stringstream ss;
        ss << "device_type = " << m_device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", has a different version published.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        if(m_notifier){
            m_notifier->stop();
        }

        auto reply2 = RedisUtil::Cmd("DEL %s", redis_device_group_key.c_str());
        if(!reply2 || reply2->type == REDIS_REPLY_ERROR){
            std::stringstream ss;
            ss << "device_type = " << m_device_type
                                    << ", name = " << name
                                    << ", version = " << version
                                    << ", redis reply: " << (!reply ? "NULL" : "REDIS_REPLY_ERROR")
                                    << ", redis error.";
            
            std::string sstr = ss.str();
            SYLAR_LOG_WARN(g_logger) << sstr;
        }
    }
    if(!m_notifier){
        std::stringstream ss;
        ss << "device_type = " << m_device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", first time publish.";
        std::string sstr = ss.str();
        SYLAR_LOG_INFO(g_logger) << sstr;

        m_notifier = std::make_shared<OTANotifier>(timer_mgr, topic, m_client, 10000);
    }

    // 5. set version in redis
    auto reply3 = RedisUtil::Cmd(redis_pool_name, "SET %s %s", redis_device_group_key.c_str(), version.c_str());
    if(!reply3 || reply3->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << m_device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply3 ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        m_notifier.reset();
        return 0;
    }

    // 6. start notify
    SYLAR_LOG_INFO(g_logger) << "device type = " << m_device_type
                                     << " start to notify.";
    m_notifier->set_message(msg);
    m_notifier->start();
    return 1;
}

int OTADevice::ota_stop_notify(const std::string& name
                               , const std::string& version
                               , const std::string& redis_pool_name){
    std::stringstream ss;
    ss << "/ota/" << m_device_type
                    << "/" << name 
                    << "/" << version
                    << "/notify";

    std::string topic = ss.str();

    if(m_notifier){
        m_notifier->stop();
    }
    const std::string redis_device_group_key{OTAHash::get_device_group_mudule_hash("notify", m_device_type, name)};
    auto reply = RedisUtil::Cmd(redis_pool_name, "DEL %s", redis_device_group_key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << m_device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        return 0;
    }
    return 1;
}

}; // namespace sherry