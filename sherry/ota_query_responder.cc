#include "ota_query_responder.h"
#include "log.h"
#include "../include/json/json.hpp"
#include "util.h"

#include <functional>

namespace sherry{

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

OTAQueryResponder::OTAQueryResponder(int device_type, int device_no, MqttClientManager::ptr cli_mgr)
    :m_device_type(device_type)
    ,m_device_no(device_no)
    ,m_client_manager(cli_mgr){
    m_client = m_client_manager->get_client(device_type);
}

std::string OTAQueryResponder::format_payload(const std::string& action){
    nlohmann::json j = {
        {"action", action},
        {"time", getCurrentTimeString()}
    };
    return j.dump();
}

void OTAQueryResponder::publish_query(const std::string& action, int qos, bool retain){
    
    std::stringstream ss = FormatOtaPrex(m_device_type, m_device_no);
    ss << "/query/";

    std::string topic = ss.str();

    std::string payload = format_payload(action);

    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_client){
            if(!m_client->get_isconnected()){
                m_client->connect(true);
            }

            if(!m_client->get_isconnected()){
                SYLAR_LOG_ERROR(g_logger) << "client is diconnect"
                                          << ", action = " << action
                                          << ", can not query."; 
                return;
            }

            m_client->publish(topic, payload, qos, retain);
            return;
        }
    }
    
    {
        RWMutexType::WriteLock lock(m_mutex);
        if(!m_client){
            SYLAR_LOG_WARN(g_logger) << "client is null";
            if(!m_client_manager){
                SYLAR_LOG_ERROR(g_logger) << "client manager is null."
                                          << ", action = " << action
                                          << ", can not query.";
                return;
            }
            m_client_manager->get_client(m_device_type);
            
            if(!m_client){
                SYLAR_LOG_WARN(g_logger) << "client is null."
                                         << ", action = " << action
                                         << ", can not query.";
                return;
            }

            m_client->connect(true);
            if(!m_client->get_isconnected()){
                SYLAR_LOG_ERROR(g_logger) << "client is diconnect"
                                          << ", action = " << action
                                          << ", can not query."; 
                return;
            }

            m_client->publish(topic, payload, qos, retain);
       
        }
    } 

}

void OTAQueryResponder::subscribe_responder(const std::string& pub_topic, const std::string& sub_topic, int qos){
    std::function<void(const std::string &, int)> subscribe_callback =
    [](const std::string& topic, int code) {
        if(code == -1){
            SYLAR_LOG_ERROR(g_logger) << "subscribe to topic : " << topic
                                      << " fail.";
        } else {
            SYLAR_LOG_INFO(g_logger) << "subscribe to topic : " << topic
                                     << " success.";
        }
    };
    
    
    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_client){
            if(!m_client->get_isconnected()){
                m_client->connect(true);
            }

            if(!m_client->get_isconnected()){
                SYLAR_LOG_ERROR(g_logger) << "client is diconnect"
                                          << " sub topic = " << sub_topic
                                          << ", can not subscribe."; 
                return;
            }

            m_client->subscribe(sub_topic, qos, subscribe_callback);
            return;
        }
    }

    {
        RWMutexType::WriteLock lock(m_mutex);
        if(!m_client){
            SYLAR_LOG_WARN(g_logger) << "client is null";
            if(!m_client_manager){
                SYLAR_LOG_ERROR(g_logger) << "client manager is null."
                                          << " sub topic = " << sub_topic
                                          << ", can not subscribe.";
                return;
            }
            m_client_manager->get_client(m_device_type);
            
            if(!m_client){
                SYLAR_LOG_WARN(g_logger) << "client is null."
                                         << " sub topic = " << sub_topic
                                         << ", can not subscribe.";
                return;
            }

            m_client->connect(true);
            if(!m_client->get_isconnected()){
                SYLAR_LOG_ERROR(g_logger) << "client is diconnect"
                                          << " sub topic = " << sub_topic
                                          << ", can not subscribe.";
                return;
            }

            m_client->subscribe(sub_topic, qos, subscribe_callback);
       
        }
    } 
}

void OTAQueryResponder::subscribe_on_success(const std::string& pub_topic, const std::string& sub_topic){
    {
        RWMutexType::ReadLock lock(m_mutex);
        if(m_client){
            if(!m_client->get_isconnected()){
                m_client->connect(true);
                if(!m_client->get_isconnected()){
                    SYLAR_LOG_ERROR(g_logger) << "client is diconnect"
                                              << " after subscribing topic: " << sub_topic
                                              << ", publish null to topic: " << pub_topic
                                              << " failed.";
                    return;
                }
            } else {
                m_client->unsubscribe(sub_topic);
            }
            m_client->publish(pub_topic, "", 1);
            return;
        }
    }


    {
        RWMutexType::WriteLock lock(m_mutex);
        if(!m_client){
            SYLAR_LOG_WARN(g_logger) << "client is null";
            if(!m_client_manager){
                SYLAR_LOG_ERROR(g_logger) << "client manager is null."
                                          << " after subscribing topic: " << sub_topic
                                          << ", publish null to topic: " << pub_topic
                                          << " failed.";
                return;
            }
            m_client_manager->get_client(m_device_type);
            
            if(!m_client){
                SYLAR_LOG_WARN(g_logger) << "client is null."
                                         << " after subscribing topic: " << sub_topic
                                         << ", publish null to topic: " << pub_topic
                                         << " failed.";
                return;
            }

            m_client->connect(true);
            if(!m_client->get_isconnected()){
                SYLAR_LOG_ERROR(g_logger) << "client is diconnect"
                                          << " after subscribing topic: " << sub_topic
                                          << ", publish null to topic: " << pub_topic
                                          << " failed.";
                return;
            }

            m_client->publish(pub_topic, "", 1);
       
        }
    } 
}

}