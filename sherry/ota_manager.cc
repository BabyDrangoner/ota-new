#include "sherry.h"
#include "ota_manager.h"
#include "../include/json/json.hpp"
#include "util.h"
#include "http/http_util.h"
#include "hash.h"
#include "db/redis.h"
#include "db/redis_util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TAG "[OTAManager]"

namespace sherry{

static const size_t REDIS_MESSAGE_QUEUE_MAX_ERROR_COUNT = 10;
static const uint64_t QUERY_RETAIN_TIME = 3600;

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static OTAManager* t_otaMgr = nullptr;

struct FileDetail{
    size_t size;
    std::string MD5;
};

OTAManager::OTAManager(size_t buffer_size, const std::string& protocol
                       , const std::string& host, int port
                       , const std::string& file_prev_path, IOManager::ptr io_mgr
                       , const std::string& redis_pool_name
                       , const std::string& redis_mq_pool_name)
    :m_protocol(protocol)
    ,m_host(host)
    ,m_port(port)
    ,m_running(true)
    ,m_stopped(false)
    ,m_file_prev_path(file_prev_path)
    ,m_buffer_size(buffer_size)
    ,m_redis_pool_name(redis_pool_name)
    ,m_redis_mq_pool_name(redis_mq_pool_name){

    m_timer_mgr = io_mgr;
    m_device_types_counts = 0;
    m_device_counts = 0;
    m_callback_mgr = std::make_shared<OTAClientCallbackManager>();
    m_client_mgr = std::make_shared<MqttClientManager>(m_port, m_protocol, m_host, m_callback_mgr);
    
    m_device_type_nums.clear();
    m_ota_notifier_map.clear();

    m_redis_message_queue_consume_thread.reset(
        new Thread(std::bind(&OTAManager::redis_message_queue_thread_run, this), "redis_mq_consumer"));
    SetThis();
}

OTAManager* OTAManager::GetThis(){
    SYLAR_LOG_DEBUG(g_logger) << "OTAManager::GetThis()";
    return t_otaMgr;
}

void OTAManager::SetThis(){
    t_otaMgr = this;
}

bool OTAManager::add_device(uint16_t device_type, uint32_t device_no){
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

bool OTAManager::remove_device(uint16_t device_type, uint32_t device_no){
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

void OTAManager::ota_notify(uint16_t device_type
                            , const std::string& name
                            , const std::string& version
                            , http::HttpResponse::ptr rsp){
    // 1. get notify message
    struct OTAMessage msg;
    if(!get_notify_message(device_type, name, version, msg)){
        
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << " get notify message error";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        (void)setHttpResponse(rsp, http::HttpStatus::NOT_FOUND
                              , "get notify message error");
        return;
    }

    // 2. get version in redis
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
        
        (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR
                             , "system error.");

        return;
    }

    // 3. check version
    if(reply->type == REDIS_REPLY_STRING && reply->str && strcmp(reply->str, version.c_str()) == 0){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", has been published.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        (void)setHttpResponse(rsp, http::HttpStatus::OK, sstr);
        return;
    }

    // 4. notify
    std::stringstream ss;
    ss << "/ota/" << device_type
       << "/" << name 
       << "/notify";
    
    std::string topic(ss.str());

    OTANotifier::ptr notifier = nullptr;
    // 检查 Redis 中是否已有版本记录（reply->str 不为 NULL 且不为空字符串）
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

        (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR
                            , "system error.");
        return;
    }

    // 6. start notify
    SYLAR_LOG_INFO(g_logger) << "device type = " << device_type
                                     << " start to notify.";
    notifier->set_message(msg);
    notifier->start();
    (void)setHttpResponse(rsp, http::HttpStatus::OK
                          , "notify task submitted.");

}

void OTAManager::ota_stop_notify(uint16_t device_type
                                , const std::string& name
                                , const std::string& version
                                , http::HttpResponse::ptr rsp){
    // 1. get version from redis
    const std::string redis_device_group_key = OTAHash::get_device_group_mudule_hash("stop_notify", device_type, name);
    auto reply = RedisUtil::Cmd("GET %s", redis_device_group_key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << ", redis reply: " << (!reply ? "NULL" : "REDIS_REPLY_ERROR")
                                << ", redis error.";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR
                             , "system error.");

        return;
    }

    // 2. check version
    if(reply->type == REDIS_REPLY_STRING && reply->str){
        if(strcmp(reply->str, version.c_str()) != 0){
            std::stringstream ss;
            ss << "device_type = " << device_type
                                    << ", name = " << name
                                    << ", current version = " << reply->str
                                    << ", need-stoping version = " << version;
            
            std::string sstr = ss.str();
            SYLAR_LOG_WARN(g_logger) << sstr;
            
            (void)setHttpResponse(rsp, http::HttpStatus::NO_CONTENT, sstr);
            return;
        }

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

            (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR,
                                 "redis error.");
            return;
        }

        (void)setHttpResponse(rsp, http::HttpStatus::OK, "stop notify successfully.");
        return;
    }

    (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR,
                                 "unknown error.");

}

void OTAManager::ota_query(uint16_t device_type
                           , uint32_t device_no
                           , const std::string& action
                           , http::HttpResponse::ptr rsp){
    const std::string device_id_key = OTAHash::get_device_id_hash("query", device_type, device_no);
    auto reply = RedisUtil::Cmd(m_redis_pool_name, "GET %s", device_id_key.c_str());
    if(!reply || reply->type == REDIS_REPLY_ERROR){
    
        SYLAR_LOG_ERROR(g_logger) << TAG
           << " device_type = " << device_type
           << ", device_no = " << device_no
           << ", action = " << action
           << ", redis error = " << (!reply ? "null" : "REDIS_REPLY_ERROR");

        (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR, "system error.");
        return;
    }

    if(reply->type != REDIS_REPLY_NIL){
        const std::string answer{reply->str};
        if(redis_reset_value(m_redis_pool_name, device_id_key, answer, QUERY_RETAIN_TIME) == 0){
            SYLAR_LOG_WARN(g_logger) << TAG
                                     << " device_type = " << device_type
                                     << ", device_no = " << device_no
                                     << ", action = " << action
                                     << ", redis reset value error.";
        }

        rsp->setStatus(http::HttpStatus::OK);
        rsp->setBody(answer);
        return;
    }

    ota_query_device(device_type, device_no, action, rsp);
    const std::string& answer = rsp->getBody();
    if(!answer.empty() &&
        redis_set_key_value(m_redis_pool_name, device_id_key, answer, QUERY_RETAIN_TIME) == 0){
        SYLAR_LOG_WARN(g_logger) << TAG
            << "ota query device_type = " <<  device_type
            << ", devcie_no = " << device_no
            << ", action = " << action
            << ", redis set key error.";
    }
    
    return;
}

void OTAManager::ota_query_device(uint16_t device_type
                                , uint32_t device_no
                                , const std::string& action
                                , http::HttpResponse::ptr rsp){
    const std::string type = "query";
    std::stringstream pub_stream, sub_stream;
    pub_stream = FormatOtaPrex(device_type, device_no);
    sub_stream = FormatOtaPrex(device_type, device_no);

    pub_stream << "/query";
    sub_stream << "/responder";

    std::string pub_topic = pub_stream.str();
    std::string sub_topic = sub_stream.str();

    sherry::Fiber::ptr thisFiber = Fiber::GetThis();
    sherry::IOManager* ioMgr = IOManager::GetThis();

    OTAQueryResponder::ptr oqr = std::make_shared<OTAQueryResponder>(device_type, device_no, m_client_mgr);
    m_callback_mgr->regist_callback(sub_topic
                                    ,[this, oqr, pub_topic, device_type, device_no, action, type, thisFiber, rsp, ioMgr]
                                    (const std::string& topic, const std::string& payload){

        oqr->subscribe_on_success(pub_topic, topic);
        try{
            SYLAR_LOG_DEBUG(g_logger) << payload;

            nlohmann::json json_response = nlohmann::json::parse(payload);
            if(!json_response.contains("query_details") 
                      || !json_response.contains("device_type")
                      || !json_response.contains("device_no")){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                         << "/" << device_no 
                                         << " details is not satisfied.";
                return;
            }
            
            if(!json_response["query_details"].contains("action") || json_response["query_details"]["action"] != action){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                          << "/" << device_no
                                          << " response action: " << json_response["action"]
                                          << " is not same with query action: " << action;
                return;
            } 
            if(json_response["device_type"] != device_type){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                          << "/" << device_no
                                          << " response device_type: " << json_response["device_type"]
                                          << " is not same with query device_type: " << device_type;
                return;
            } 
            if(json_response["device_no"] != device_no){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                          << "/" << device_no
                                          << " response device_no: " << json_response["device_no"]
                                          << " is not same with query device_no: " << device_no;
                return;
            }

            rsp->setBody(json_response.dump());
            rsp->setStatus(http::HttpStatus::OK);

            while(true){
                Fiber::State st = thisFiber->getState();
                if(st == Fiber::State::HOLD) break;
                else if(st == Fiber::State::EXCEPT || st == Fiber::State::TERM) return;
            }

            if(ioMgr){
                ioMgr->schedule(thisFiber);
            }else{
                setServerError(rsp);
            }
            
        } catch(const std::exception& e){
            nlohmann::json json_response;
            
            std::stringstream ss;

            ss << "Query device:" << device_type
               << "/" << device_no 
               << "error, error is " << e.what();

            std::string str = ss.str();

            json_response["msg"] = std::move(str);

            rsp->setBody(json_response.dump());
            rsp->setStatus(http::HttpStatus::OK);

            while(true){
                Fiber::State st = thisFiber->getState();
                if(st == Fiber::State::HOLD) break;
                else if(st == Fiber::State::EXCEPT || st == Fiber::State::TERM) return;
            }

            if(ioMgr){
                ioMgr->schedule(thisFiber);
            }else{
                setServerError(rsp);
            }
            return;
        }
        
    });
    oqr->publish_query(action, 1, true);
    oqr->subscribe_responder(pub_topic, sub_topic, 1);

    Fiber::YieldToHold();

}

void OTAManager::ota_query_download(uint16_t device_type
                                    , uint32_t device_no
                                    , const std::string& name
                                    , http::HttpResponse::ptr rsp){
    const std::string type = "query_download";

    std::stringstream ss;
    ss << "/ota/" << device_type
            << "/" << device_no 
            << "/" << name
            << "/query_download";

    std::string topic = ss.str();

    sherry::Fiber::ptr thisFiber = Fiber::GetThis();
    sherry::IOManager* ioMgr = IOManager::GetThis();

    m_callback_mgr->regist_callback(topic, [this, device_type, device_no, name, type, thisFiber, rsp, ioMgr]
                                    (const std::string& topic, const std::string& payload){

        try{
            SYLAR_LOG_DEBUG(g_logger) << payload;

            nlohmann::json json_response = nlohmann::json::parse(payload);
            if(!json_response.contains("name") 
                      || !json_response.contains("device_type")
                      || !json_response.contains("device_no")){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                         << "/" << device_no 
                                         << " details is not satisfied.";
                return;
            }
            
            if(json_response["name"] != name){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                          << "/" << device_no
                                          << " response name: " << json_response["name"]
                                          << " is not same with query action: " << name;
                return;
            } 

            if(json_response["device_type"] != device_type){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                          << "/" << device_no
                                          << " response device_type: " << json_response["device_type"]
                                          << " is not same with query device_type: " << device_type;
                return;
            } 

            if(json_response["device_no"] != device_no){
                SYLAR_LOG_WARN(g_logger) << "Query device:" << device_type
                                          << "/" << device_no
                                          << " response device_no: " << json_response["device_no"]
                                          << " is not same with query device_no: " << device_no;
                return;
            }

            rsp->setBody(json_response.dump());
            rsp->setStatus(http::HttpStatus::OK);

            while(true){
                Fiber::State st = thisFiber->getState();
                if(st == Fiber::State::HOLD) break;
                else if(st == Fiber::State::EXCEPT || st == Fiber::State::TERM) return;
            }

            if(ioMgr){
                ioMgr->schedule(thisFiber);
            }else{
                setServerError(rsp);
            }

        } catch(const std::exception& e){
            nlohmann::json json_response;
            
            std::stringstream ss;

            ss << "Query device:" << device_type
               << "/" << device_no 
               << "error, error is " << e.what();

            std::string str = ss.str();

            json_response["msg"] = std::move(str);

            rsp->setBody(json_response.dump());
            rsp->setStatus(http::HttpStatus::OK);

            while(true){
                Fiber::State st = thisFiber->getState();
                if(st == Fiber::State::HOLD) break;
                else if(st == Fiber::State::EXCEPT || st == Fiber::State::TERM) return;
            }

            if(ioMgr){
                ioMgr->schedule(thisFiber);
            }else{
                setServerError(rsp);
            }

            return;
        }
        
    });

    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_ota_subscribe_download_map.find(device_type);
        if(it != m_ota_subscribe_download_map.end()){
            auto itt = (*it).second.find(device_no);
            if(itt != (*it).second.end()){
                (*itt).second->subscribe_download(topic);
                SYLAR_LOG_INFO(g_logger) << "device_type = " << device_type
                                         << ", device_no = " << device_no
                                         << ", name = " << name
                                         << " start to subscribe download detail.";
                return;
            }
        }
    }

    OTASubscribeDownload::ptr ota_sd = nullptr;
    {
        RWMutexType::WriteLock lock(m_mutex);
        ota_sd = std::make_shared<OTASubscribeDownload>(topic, device_type, device_no, m_client_mgr, m_callback_mgr);
        m_ota_subscribe_download_map[device_type][device_no] = ota_sd;
    }
    ota_sd->subscribe_download(topic);
    SYLAR_LOG_INFO(g_logger) << "device_type = " << device_type
                             << ", device_no = " << device_no
                             << ", name = " << name
                             << " start to subscribe download detail.";
    Fiber::YieldToHold();

}

void OTAManager::ota_file_download(uint16_t device_type
                                  , const std::string& name
                                  , const std::string& version
                                  , http::HttpResponse::ptr rsp
                                  , http::HttpSession::ptr session){
    const std::string type = "file_download";

    std::string file_name = "ota_" 
                            + std::to_string(device_type) 
                            + "_" + version 
                            + "_" + name + ".jpg";
    std::string file_path = m_file_prev_path + file_name;

    struct FileDetail file_detail;
    int fd = getFileDetail(file_path, file_detail);
    if(fd < 0){
        rsp->setStatus(http::HttpStatus::NOT_FOUND);
        rsp->setBody("no file.");
        return;
    }

    size_t file_size = file_detail.size;

    rsp->setIsSending(true);
    rsp->setStatus(http::HttpStatus::OK);
    rsp->setHeader("Content-Length", std::to_string(file_size));
    rsp->setHeader("X-Content-MD5", file_detail.MD5);
    rsp->setHeader("Content-Disposition", "attachment; filename=" + file_name);
    rsp->setHeader("Content-Type", "image/jpeg");
    rsp->setClose(true);

    sherry::Fiber::ptr thisFiber = Fiber::GetThis();
    sherry::IOManager* ioMgr = IOManager::GetThis();

    off_t offset = 0;

    int sockfd = session->getSocket()->getSocket();
    ioMgr->addEvent(sockfd, IOManager::Event::WRITE, [fd, thisFiber, file_size, offset, session, rsp](){
        while(rsp->isSending()){}
        rsp->setIsSending(false);
        sendFile(fd, thisFiber, offset, file_size, session);
    });

}

int OTAManager::send_file(int fd, off_t* offset, size_t file_size, http::HttpSession::ptr session){
    return session->sendfile(fd, offset, file_size);
}

void OTAManager::sendFile(int fd, Fiber::ptr thisFiber, off_t offset, size_t file_size, http::HttpSession::ptr session){

    int rt = send_file(fd, &offset, file_size, session);
    SYLAR_LOG_DEBUG(g_logger) << "sendFile file_size: " << file_size;

    if(rt == -1){
        SYLAR_LOG_WARN(g_logger) << "sendfile error: " << strerror(errno);
        return;
    }

    file_size -= rt;

    if(file_size == 0){
        IOManager::GetThis()->schedule(thisFiber);
        SYLAR_LOG_DEBUG(g_logger) << "sendFile return";
        return;
    }

    int sockfd = session->getSocket()->getSocket();
    IOManager::GetThis()->addEvent(sockfd, IOManager::Event::WRITE, [fd, thisFiber, file_size, offset, session](){
        sendFile(fd, thisFiber, offset, file_size, session);
    });

}

int OTAManager::getFileDetail(const std::string& file_path, struct FileDetail& file_detail){

    int file_fd = open(file_path.c_str(), O_RDONLY);
    if(file_fd < 0){
        SYLAR_LOG_WARN(g_logger) << "open file: " << file_path 
                                 << " failed, error = " << strerror(errno);
        return -1;
    }
    struct stat st;
    if (fstat(file_fd, &st) != 0) {
        close(file_fd);
        return -1;
    }

    file_detail.size = st.st_size;
    file_detail.MD5 = "ed076287532e86365e841e92bfc50d8c";

    return file_fd;
}


bool OTAManager::get_notify_message(uint16_t device_type, const std::string& name, const std::string& version, struct OTAMessage& msg){
    
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

bool OTAManager::check_device(uint16_t device_type, uint32_t device_no){
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

bool OTAManager::check_device(uint16_t device_type){
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_device_type_nums.find(device_type);
    if(it == m_device_type_nums.end()){
        return false;
    }

    return true;
}

void OTAManager::redis_message_queue_thread_run(){
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

void OTAManager::redis_message_queue_thread_command_dispatch(const std::string& command){
    SYLAR_LOG_INFO(g_logger) << TAG
        << "redis command " << command;
}

void setHttpResponse(http::HttpResponse::ptr rsp, http::HttpStatus status
                    , const std::string& msg){
    
    nlohmann::json j;
    j["msg"] = std::move(msg);
    
    rsp->setBody(j.dump());
    rsp->setStatus(status);
}

}