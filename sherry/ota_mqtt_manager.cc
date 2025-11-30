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

OTAMqttManager::OTAMqttManager(size_t buffer_size, const std::string& protocol
                       , const std::string& host, int port
                       , const std::string& redis_pool_name
                       , const std::string& redis_mq_pool_name)
    :m_protocol(protocol)
    ,m_host(host)
    ,m_port(port)
    ,m_running(true)
    ,m_stopped(false)
    ,m_buffer_size(buffer_size)
    ,m_redis_pool_name(redis_pool_name)
    ,m_redis_mq_pool_name(redis_mq_pool_name){

    m_device_types_counts = 0;
    m_device_counts = 0;
    m_callback_mgr = std::make_shared<OTAClientCallbackManager>();
    m_client_mgr = std::make_shared<MqttClientManager>(m_port, m_protocol, m_host, m_callback_mgr);
    
    m_device_type_nums.clear();
    m_ota_notifier_map.clear();

    // 初始化命令分发器
    m_command_dispatcher = std::make_shared<OTACommandDispatcher>(this);
    // 创建命令处理的 IOManager
    m_command_ioMgr = std::make_shared<IOManager>(2, false, "ota_cmd_worker");
    m_timer_mgr = std::make_shared<IOManager>(4, false, "ota_timer_worker");

    m_redis_message_queue_consume_thread.reset(
        new Thread(std::bind(&OTAMqttManager::redis_message_queue_thread_run, this), "redis_mq_consumer"));
    SetThis();
}

OTAMqttManager* OTAMqttManager::GetThis(){
    SYLAR_LOG_DEBUG(g_logger) << "OTAMqttManager::GetThis()";
    return t_otaMgr;
}

void OTAMqttManager::SetThis(){
    t_otaMgr = this;
}

bool OTAMqttManager::add_device(uint16_t device_type, uint32_t device_no){
    RWMutexType::WriteLock lock(m_mutex);
    auto it = m_device_type_nums.find(device_type);
    if(it == m_device_type_nums.end()){
        ++m_device_types_counts;
        ++m_device_counts;
        m_device_type_nums[device_type].insert(device_no);
        SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                 << ", device no = " << device_no
                                 << " success add.";
        return true;
    } 

    auto itt = (*it).second.find(device_no);
    if(itt == (*it).second.end()){
        ++m_device_counts;
        (*it).second.insert(device_no);
        SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                 << ", device no = " << device_no
                                 << " has been added successfully.";
        return true;
    }

    SYLAR_LOG_WARN(g_logger) << "device type = " << device_type
                                 << ", device no = " << device_no
                                 << " has been already added.";
    return false;
}

bool OTAMqttManager::remove_device(uint16_t device_type, uint32_t device_no){
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_device_type_nums.find(device_type);
        if(it == m_device_type_nums.end()){
            SYLAR_LOG_WARN(g_logger) << "device type = " << device_type
                                    << ", device no = " << device_no
                                    << " has not been added.";
            return false;
        } 

        auto itt = (*it).second.find(device_no);
        if(itt == (*it).second.end()){
            ++m_device_counts;
            (*it).second.insert(device_no);
            SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                    << ", device no = " << device_no
                                    << " has not been added.";
            return false;
        }
    }

    RWMutexType::WriteLock lock(m_mutex);
    m_device_type_nums[device_type].erase(device_no);
    --m_device_counts;
    if(m_device_type_nums[device_type].size() == 0){
        m_device_type_nums.erase(device_type);
        --m_device_types_counts;
        
        SYLAR_LOG_DEBUG(g_logger) << "其他功能写完记得补充";
    }

    SYLAR_LOG_WARN(g_logger) << "device type = " << device_type
                                 << ", device no = " << device_no
                                 << " has been removed successfully.";
    return true;
}

void OTAMqttManager::ota_notify(uint16_t device_type
                            , const std::string& name
                            , const std::string& version){
    struct OTAMessage msg;
    if(!OTANotifier::get_notify_message(device_type, name, version, msg)){
        
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << " get notify message error";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        return;
    }
    std::stringstream ss;
    ss << "/ota/" << device_type
       << "/" << name 
       << "/notify";
    
    std::string topic(ss.str());

    OTANotifier::ptr notifier = nullptr;
    // 检查 Redis 中是否已有版本记录（reply->str 不为 NULL 且不为空字符串）
    const std::string redis_device_group_key{OTAHash::get_device_group_mudule_hash("notify", device_type, name)};
    auto reply = RedisUtil::Cmd(m_redis_pool_name, "GET %s", redis_device_group_key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        return;
    }
    if(reply->type == REDIS_REPLY_STRING && reply->str && reply->str[0] != '\0'){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", has a different version published.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        {
            RWMutexType::ReadLock lock(m_notifier_mutex);
            auto it = m_ota_notifier_map.find(topic);
            if(it != m_ota_notifier_map.end()){
                notifier = (*it).second;
            }
        }
        
        if(notifier){
            notifier->stop();
        }

        auto reply2 = RedisUtil::Cmd("DEL %s", redis_device_group_key.c_str());
        if(!reply2 || reply2->type == REDIS_REPLY_ERROR){
            std::stringstream ss;
            ss << "device_type = " << device_type
                                    << ", name = " << name
                                    << ", version = " << version
                                    << ", redis reply: " << (!reply ? "NULL" : "REDIS_REPLY_ERROR")
                                    << ", redis error.";
            
            std::string sstr = ss.str();
            SYLAR_LOG_WARN(g_logger) << sstr;
        }
    }
    if(!notifier){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", first time publish.";
        std::string sstr = ss.str();
        SYLAR_LOG_INFO(g_logger) << sstr;

        notifier = std::make_shared<OTANotifier>(device_type, m_timer_mgr
                                                , topic, m_client_mgr, 10000);
        {
            RWMutexType::WriteLock lock(m_notifier_mutex);
            m_ota_notifier_map[topic] = notifier;
        }
    }

    // 5. set version in redis
    auto reply3 = RedisUtil::Cmd(m_redis_pool_name, "SET %s %s", redis_device_group_key.c_str(), version.c_str());
    if(!reply3 || reply3->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply3 ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        {
            RWMutexType::WriteLock lock(m_notifier_mutex);
            m_ota_notifier_map.erase(topic);
        }
        return;
    }

    // 6. start notify
    SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                     << " start to notify.";
    notifier->set_message(msg);
    notifier->start();

}

void OTAMqttManager::ota_stop_notify(uint16_t device_type
                                , const std::string& name
                                , const std::string& version){
    std::stringstream ss;
    ss << "/ota/" << device_type
                    << "/" << name 
                    << "/" << version
                    << "/notify";

    std::string topic = ss.str();

    OTANotifier::ptr notifier = nullptr;
    {
        RWMutexType::ReadLock lock(m_notifier_mutex);
        auto it = m_ota_notifier_map.find(topic);
        if(it == m_ota_notifier_map.end()){
            std::stringstream ss;
            ss << "device_type = " << device_type
                << ", name = " << name
                << ", version = " << version
                << " notifier has not existed.";
            std::string sstr = ss.str();                 
            SYLAR_LOG_WARN(g_logger) << sstr;                
        } else {
            notifier = (*it).second;
            m_ota_notifier_map.erase(it);
        }
    }
    if(notifier){
        notifier->stop();
    }
    const std::string redis_device_group_key{OTAHash::get_device_group_mudule_hash("notify", device_type, name)};
    auto reply3 = RedisUtil::Cmd(m_redis_pool_name, "DEL %s", redis_device_group_key.c_str());
    if(!reply3 || reply3->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply3 ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        return;
    }
    return;
}

bool OTAMqttManager::check_device(uint16_t device_type, uint32_t device_no){
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_device_type_nums.find(device_type);
    if(it == m_device_type_nums.end()){
        return false;
    }

    auto itt = (*it).second.find(device_no);
    if(itt == (*it).second.end()){
        return false;
    }

    return true;
}

bool OTAMqttManager::check_device(uint16_t device_type){
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_device_type_nums.find(device_type);
    if(it == m_device_type_nums.end()){
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
    while(!this->is_stopped()){
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