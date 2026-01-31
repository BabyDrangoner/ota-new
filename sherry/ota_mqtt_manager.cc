#include "sherry.h"
#include "ota_mqtt_manager.h"
#include "ota_command_dispatcher.h"
#include "../include/json/json.hpp"
#include "util.h"
#include "http/http_util.h"
#include "hash.h"
#include "db/redis.h"
#include "db/redis_util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TAG "[OTAMqttManager]"

namespace sherry{

static const size_t REDIS_MESSAGE_QUEUE_MAX_ERROR_COUNT = 10;
static const uint64_t QUERY_RETAIN_TIME = 3600;

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static OTAMqttManager* t_otaMgr = nullptr;

struct FileDetail{
    size_t size;
    std::string MD5;
};

OTAMqttManager::OTAMqttManager(size_t buffer_size
                               , OTAClientCallbackManager::ptr cb_mgr
                               , const std::string& redis_pool_name
                               , const std::string& redis_mq_pool_name)
    :m_running(false)
    ,m_buffer_size(buffer_size)
    ,m_device_counts(0)
    ,m_callback_mgr(cb_mgr)
    ,m_redis_pool_name(redis_pool_name)
    ,m_redis_mq_pool_name(redis_mq_pool_name){
    
    m_devices.clear();

    // 初始化命令分发器
    m_command_dispatcher = std::make_shared<OTACommandDispatcher>(this);
    // 创建命令处理的 IOManager
    m_command_ioMgr = std::make_shared<IOManager>(2, false, "ota_cmd_worker");
    m_timer_mgr = std::make_shared<IOManager>(4, false, "ota_timer_worker");

    // 从配置文件中加载 MQTT 设备配置
    auto base_config = Config::LookupBase("mqtt.devices");
    if(base_config) {
        auto devices_config = std::dynamic_pointer_cast<ConfigVar<std::vector<MqttDeviceConfig>>>(base_config);
        if(devices_config) {
            auto devices = devices_config->getValue();
            SYLAR_LOG_INFO(g_logger) << TAG << " Loading " << devices.size() << " MQTT devices from config";
            
            for(const auto& dev : devices) {
                SYLAR_LOG_INFO(g_logger) << TAG
                    << " Registering device_type=" << dev.device_type
                    << " protocol=" << dev.protocol
                    << " host=" << dev.host
                    << " port=" << dev.port;
                
                bool ret = add_device(dev.device_type, dev.protocol, dev.host, dev.port,
                                      m_callback_mgr, dev.sub_topics, dev.sub_qos,
                                      dev.redis_keys, dev.expire_seconds);
                if(ret) {
                    SYLAR_LOG_INFO(g_logger) << TAG
                        << " Device type " << dev.device_type << " registered successfully";
                } else {
                    SYLAR_LOG_ERROR(g_logger) << TAG
                        << " Failed to register device type " << dev.device_type;
                }
            }
        } else {
            SYLAR_LOG_ERROR(g_logger) << TAG << " Failed to cast mqtt.devices config";
        }
    } else {
        SYLAR_LOG_WARN(g_logger) << TAG << " No MQTT devices config found";
    }

    SetThis();
}

OTAMqttManager* OTAMqttManager::GetThis(){
    SYLAR_LOG_DEBUG(g_logger) << "OTAMqttManager::GetThis()";
    return t_otaMgr;
}

void OTAMqttManager::SetThis(){
    t_otaMgr = this;
}

void OTAMqttManager::start(){
    m_running = true;
    m_redis_message_queue_consume_thread.reset(
        new Thread(std::bind(&OTAMqttManager::redis_message_queue_thread_run, this), "redis_mq_consumer"));
    for(auto it = m_devices.begin();it != m_devices.end();++it){
        it->second->start();
    }
}

void OTAMqttManager::stop(){
    m_running = false;
    for(auto it = m_devices.begin();it != m_devices.end();++it){
        it->second->stop();
    }
    m_redis_message_queue_consume_thread->join();
}

bool OTAMqttManager::add_device(uint16_t device_type
                                , const std::string& protocol
                                , const std::string& host
                                , int port
                                , OTAClientCallbackManager::ptr cb_mgr
                                , const std::vector<std::string>& sub_topics
                                , const std::vector<int>& sub_qos
                                , const std::vector<std::string>& redis_keys
                                , const std::vector<int> redis_expire_seconds){
    RWMutexType::WriteLock lock(m_device_mutex);
    auto it = m_devices.find(device_type);
    if(it == m_devices.end()){
        ++m_device_counts;
        // 创建临时的非 const vector 用于传递给 OTADevice 构造函数
        std::vector<std::string> topics(sub_topics);
        std::vector<int> qos(sub_qos);
        m_devices.emplace(device_type
            , std::make_shared<OTADevice>(device_type, port
                                          , protocol
                                          , host
                                          , cb_mgr
                                          , topics
                                          , qos
                                          , redis_keys
                                          , redis_expire_seconds
                                          , m_redis_pool_name));
        SYLAR_LOG_INFO(g_logger) << TAG
            << "device type = " << device_type
            << " added successfully.";
        return true;
    } 
    
    SYLAR_LOG_INFO(g_logger) << TAG
        << "device type = " << device_type
        << " has been already added.";
    return false;
}

bool OTAMqttManager::remove_device(uint16_t device_type){
    {
        RWMutexType::ReadLock lock(m_device_mutex);
        auto it = m_devices.find(device_type);
        if(it == m_devices.end()){
            SYLAR_LOG_WARN(g_logger) << TAG
                << "device type = " << device_type
                << " has not been added.";
            return false;
        } 
    }

    RWMutexType::WriteLock lock(m_device_mutex);
    m_devices.erase(device_type);
    --m_device_counts;

    SYLAR_LOG_INFO(g_logger) << TAG
        << "device type = " << device_type
        << " has been removed successfully.";
    return true;
}

int OTAMqttManager::ota_notify(uint16_t device_type
                              , const std::string& name
                              , const std::string& version){
    RWMutexType::ReadLock lock(m_device_mutex);
    auto it = m_devices.find(device_type);
    if(it == m_devices.end()){
        SYLAR_LOG_WARN(g_logger) << TAG
            << "ota notify device " << device_type
            << " not exits";
        return 0;
    }
    return it->second->ota_notify(name, version, m_timer_mgr, m_redis_pool_name);
}

int OTAMqttManager::ota_stop_notify(uint16_t device_type
                                , const std::string& name
                                , const std::string& version){
    RWMutexType::ReadLock lock(m_device_mutex);
    auto it = m_devices.find(device_type);
    if(it == m_devices.end()){
        SYLAR_LOG_WARN(g_logger) << TAG
            << "ota stop notify device " << device_type
            << " not exits";
        return 0;
    }
    return it->second->ota_stop_notify(name, version, m_redis_pool_name);
}

bool OTAMqttManager::check_device(uint16_t device_type){
    RWMutexType::ReadLock lock(m_device_mutex);
    auto it = m_devices.find(device_type);
    if(it == m_devices.end()){
        return false;
    }

    return true;
}

void OTAMqttManager::redis_message_queue_thread_run(){
    SYLAR_LOG_INFO(g_logger) << TAG
        << " redis message queue thread start running.";
    
    // SYLAR_ASSERT(ota_mgr);
    // SYLAR_ASSERT(message_queue_key.size());
    
    size_t error_cnt = 0;
    auto redis_mq_key = OTAHash::get_message_queue_hash();
    while(this->is_running()){
        auto reply = RedisUtil::Cmd(m_redis_mq_pool_name, "BRPOP %s 0"
                                    , redis_mq_key.c_str());
        if(!reply){
            SYLAR_LOG_ERROR(g_logger) << TAG
                << " redis message queue thread error, reply is null";
            ++error_cnt;
        } else {
            switch (reply->type){
                case REDIS_REPLY_ARRAY:
                    // BRPOP 返回 [key, value]，所以 elements 应该是 2
                    if(reply->elements == 2 && reply->element[1]->type == REDIS_REPLY_STRING){
                        // element[1] 才是真正的消息内容
                        this->redis_message_queue_thread_command_dispatch(std::string(reply->element[1]->str));
                        error_cnt = 0;
                    } else {
                        SYLAR_LOG_ERROR(g_logger) << TAG
                            << " redis message queue thread reply format error, elements=" << reply->elements;
                        ++error_cnt;
                    }
                    break;
                case REDIS_REPLY_NIL:
                    // 超时返回 NIL，这里设了 0 理论上一直阻塞，但防守性编程加上
                    error_cnt = 0;
                    break;
                default:
                    SYLAR_LOG_DEBUG(g_logger) << TAG
                        << " redis message queue thread reply type error, type = "
                        << reply->type;
                    ++error_cnt;
                    break;
            }
        }

        if(error_cnt > REDIS_MESSAGE_QUEUE_MAX_ERROR_COUNT){
            SYLAR_LOG_ERROR(g_logger) << TAG
                << " redis message queue thread error cnt > MAX COUNT, thread exit";
            break;
        }
    }

    SYLAR_LOG_INFO(g_logger) << TAG
        << " redis message queue thread exit.";
    
}

void OTAMqttManager::redis_message_queue_thread_command_dispatch(const std::string& command){
    SYLAR_LOG_INFO(g_logger) << TAG
        << " received command: " << command;

    if (!m_command_dispatcher) {
        SYLAR_LOG_ERROR(g_logger) << TAG
            << " command dispatcher is null";
        return;
    }

    if (!m_command_ioMgr) {
        SYLAR_LOG_ERROR(g_logger) << TAG
            << " command IOManager is null";
        return;
    }

    // 使用 IOManager 异步调度命令处理
    m_command_ioMgr->schedule([this, command]() {
        m_command_dispatcher->dispatch(command);
    });
}

}