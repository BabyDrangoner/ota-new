#include "mqtt_client.h"
#include "log.h"
#include "sherry.h"
#include "mqtt_listener.h"

namespace sherry{

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

MqttClient::MqttClient(const std::string& protocol, int port, const std::string& host,
    const std::string& client_id, OTAClientCallbackManager::ptr cbmgr)
    :m_port(port)
    ,m_protocol(protocol)
    ,m_host(host)
    ,m_client_id(client_id)
    ,m_serverAddress(sherry::MqttAddress::ptr(new sherry::MqttAddress(protocol, port, host)))
    ,m_client(new mqtt::async_client(m_serverAddress->getAddr(), m_client_id))
    ,m_cbmgr(cbmgr){
    m_client->set_callback(*this);
}

void MqttClient::CreateMqttClient(mqtt::async_client * cli){
    if(cli && (*cli).is_connected()) {
        (*cli).disconnect()->wait();
        delete(m_client);
    }
    cli = new mqtt::async_client(m_serverAddress->getAddr(), m_client_id);
}

void MqttClient::set_port(int v){
    m_port = v;
    m_serverAddress->setPort(m_port);
    
    CreateMqttClient(m_client);
}

void MqttClient::set_serverAddr(const std::string& protocol, int port, const std::string& host){
    m_serverAddress->setAddr(protocol, port, host);
    m_protocol = protocol;
    m_port = port;
    m_host = host;
    CreateMqttClient(m_client);
}

void MqttClient::set_connOpts(int KAInterval, bool AutoRconn, bool CleanSesstion,
                    const std::string& UserName, const std::string& PassWord,
                    int ConnTimeout){
    m_connOpts.set_keep_alive_interval(KAInterval);
    m_connOpts.set_automatic_reconnect(AutoRconn);
    m_connOpts.set_clean_session(CleanSesstion);
    m_connOpts.set_user_name(UserName);
    m_connOpts.set_password(PassWord);
    m_connOpts.set_connect_timeout(ConnTimeout);
} 

void MqttClient::connect(bool use_opts){
    SYLAR_ASSERT((!m_client->is_connected()));

    if(use_opts){
        try{
            auto tok =  m_client->connect(m_connOpts);
            tok->wait();
            SYLAR_LOG_INFO(g_logger) << "mqtt client : " << m_client_id 
                                     << " connect to " << m_serverAddress->getAddr() << " sucess";
            m_isconnected = true;    
        } catch (const mqtt::exception& e){
            SYLAR_LOG_ERROR(g_logger) << "mqtt client : " << m_client_id 
                                      << " connect to " << m_serverAddress->getAddr() << " fail."
                                      << " error is " << e.what();
        }
    } else {
        try{
            auto tok =  m_client->connect();
            tok->wait();
            SYLAR_LOG_INFO(g_logger) << "mqtt client : " << m_client_id 
                                     << " connect to " << m_serverAddress->getAddr() << " sucess";
            m_isconnected = true;    
        } catch (const mqtt::exception& e){
            SYLAR_LOG_ERROR(g_logger) << "mqtt client : " << m_client_id 
                                      << " connect to " << m_serverAddress->getAddr() << " fail."
                                      << " error is " << e.what();
        }
    }
        
}

void MqttClient::subscribe(const std::string& topic, int qos, std::function<void(const std::string&, int)> callback){
    SYLAR_ASSERT(m_client->is_connected() && m_isconnected);
    
    if(qos < 0 || qos > 2){
        SYLAR_LOG_WARN(g_logger) << "Invalid Qos level: ";
        qos = 1;
    }

    try{
        auto token = m_client->subscribe(topic, qos);
        if(callback){
            m_listener.set_topic_cb(topic, callback);
            token->set_action_callback(m_listener);
        }
        
        SYLAR_LOG_INFO(g_logger) << "mqtt client: " << m_client_id;

    } catch (const mqtt::exception & e){
        SYLAR_LOG_ERROR(g_logger) << "mqtt client : " << m_client_id 
                                  << " subscribe topic  " << topic << " fail."
                                  << " error is " << e.what();
        return;    
    }

}

void MqttClient::publish(const std::string& topic, const std::string payload, int qos, bool retained, std::function<void(const std::string&, int)> callback){
    if(!m_client->is_connected()) SYLAR_LOG_DEBUG(g_logger) << "client disconnected";
    SYLAR_ASSERT(m_client->is_connected() && m_isconnected);

    if(qos < 0 || qos > 2){
        SYLAR_LOG_WARN(g_logger) << "Invalid Qos: " << qos
                                 << ", fallback to 1.";
        qos = 1;
    }

    try{
        auto message = mqtt::make_message(topic, payload);
        message->set_qos(qos);
        message->set_retained(retained);

        auto token = m_client->publish(message);

        if(callback){
            m_listener.set_topic_cb(topic, callback);
            token->set_action_callback(m_listener);
        }

    } catch(const mqtt::exception& e){
        SYLAR_LOG_ERROR(g_logger) << "mqtt client: " << m_client_id
                                  << " publish to topic " << topic << " failed. "
                                  << "error: " << e.what();
        if (callback) {
            callback(topic, -1);  // 异常也回调
        }
    }
}

void MqttClient::disconnect(std::function<void(int)> callback, int timeout){
    if(!m_client->is_connected()){
        SYLAR_LOG_WARN(g_logger) << "MQTT client:" << m_client_id
                                 << " is already disconnected.";
        if(callback) callback(0);
        return;
    }

    try{
        mqtt::token_ptr token;

        if(timeout <= 0){
            token = m_client->disconnect();
        } else {
            token = m_client->disconnect(timeout);
        }

        if(callback){
            auto listener = std::make_shared<MqttDisconnectListener>(std::move(callback));
            token->set_action_callback(*listener);
        }

        SYLAR_LOG_INFO(g_logger) << "MQTT client: " << m_client_id 
                                 << " is disconnecting...";
    } catch(const mqtt::exception& e){
        SYLAR_LOG_ERROR(g_logger) << "MQTT client: " << m_client_id
                                  << " disconnect failed. Error: " << e.what();
        if(callback) callback(-1);
    }
    
}

void MqttClient::unsubscribe(const std::string& topic, std::function<void(const std::string&, int)> callback){
    if(!m_client->is_connected()){
        SYLAR_LOG_WARN(g_logger) << "MQTT client: " << m_client_id
                                 << " is already disconnected. cannot subscribe.";
        return;
    }
    try{
        auto token = m_client->unsubscribe(topic);
        if(callback){
            auto listener = std::make_shared<MqttActionListener>(topic, std::move(callback));
            token->set_action_callback(*listener);
        }

        SYLAR_LOG_INFO(g_logger) << "Unsubscribing from topic: " << topic;
    } catch (const mqtt::exception& e){
        SYLAR_LOG_ERROR(g_logger) << "Unsubscribe failed for topic " << topic
                                  << ". Error: " << e.what();
        if(callback) callback(topic, -1);
    }
}

void MqttClient::connected(const std::string& cause){
    SYLAR_LOG_INFO(g_logger) << "MQTT connected. Cause: " << cause;

    m_isconnected = true;
}

void MqttClient::connection_lost(const std::string& cause){
    SYLAR_LOG_WARN(g_logger) << "MQTT connection lost. Cause: " << cause;

    m_isconnected = false;
}

void MqttClient::message_arrived(mqtt::const_message_ptr msg){
    if(m_cbmgr) m_cbmgr->on_message(msg->get_topic(), msg->to_string());
}

void MqttClient::delivery_complete(mqtt::delivery_token_ptr tok) {
    if (!tok) {
        SYLAR_LOG_WARN(g_logger) << "[MQTT] delivery_complete: token is null.";
        return;
    }

    int msg_id = tok->get_message_id();
    SYLAR_LOG_INFO(g_logger) << "Delivery complete. Message ID: " << msg_id;
}

MqttClientManager::MqttClientManager(int port, const std::string& protocol, const std::string& host, OTAClientCallbackManager::ptr cb_mgr)
    :m_port(port)
    ,m_protocol(protocol)
    ,m_host(host)
    ,m_cbMgr(cb_mgr){
    m_clients.clear();
    m_client_num = 0;
    m_client_id = 0;
}

MqttClient::ptr MqttClientManager::get_client(int device_type){
    if(device_type < 0){
        SYLAR_LOG_ERROR(g_logger) << "device type = " << device_type
                                  << " device type < 0.";
        return nullptr;
    }

    {
        // RWMutexType::ReadLock lock(m_mutex);
        auto it = m_clients.find(device_type);
        if(it != m_clients.end() && (*it).second) return (*it).second;
    }

    // RWMutexType::WriteLock lock(m_mutex);
    m_clients[device_type] = std::make_shared<MqttClient>(
        m_protocol
        ,m_port
        ,m_host
        ,std::to_string(m_client_id)
        ,m_cbMgr
    );

    if(!m_clients[device_type]){
        SYLAR_LOG_ERROR(g_logger) << "device type = " << device_type
                                  << " client can not be create.";
        return nullptr;
    } else SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                    << " client create sucess!";
    ++m_client_num;
    ++m_client_id;
    return m_clients[device_type];

}

void MqttClientManager::delete_client(int device_type){
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_clients.find(device_type);
        if(it == m_clients.end()){
            SYLAR_LOG_WARN(g_logger) << "device type = " << device_type
                                     << " client has not created before.";
            return;
        }
    }

    RWMutexType::WriteLock lock(m_mutex);
    MqttClient::ptr cli = m_clients[device_type];
    if(cli.use_count() > 1) {
        SYLAR_LOG_WARN(g_logger) << "device type = " << device_type
                                 << " client's use_count > 1, use_count = " << cli.use_count();
        return; 
    }
    cli->disconnect();
    m_clients.erase(device_type);
    --m_client_num;
    SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                 << " delete sucessfully.";
}

}
