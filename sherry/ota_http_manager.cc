#include "ota_http_manager.h"
#include "ota_mqtt_manager.h"
#include "log.h"
#include "hash.h"
#include "db/redis_util.h"
#include "../include/json/json.hpp"
#include "db/redis.h"
#include "ota_notifier.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TAG "[OTAHttpManager]"

namespace sherry {

static Logger::ptr g_logger = SYLAR_LOG_ROOT();

struct FileDetail{
    size_t size;
    std::string MD5;
};

static void setHttpResponse(http::HttpResponse::ptr rsp, http::HttpStatus status
                            , const std::string& msg){
    
    nlohmann::json j;
    j["msg"] = std::move(msg);
    
    rsp->setBody(j.dump());
    rsp->setStatus(status);
}

OTAHttpManager::OTAHttpManager(const std::string& redis_http_pool_name
                               , const std::string& file_prev_path
                               , const std::string& ota_html_content)
    : m_redis_pool_name(redis_http_pool_name)
    , m_file_prev_path(file_prev_path)
    , m_ota_html_content(ota_html_content) {
    SYLAR_LOG_INFO(g_logger) << TAG << " initialized with redis pool: " << m_redis_pool_name;
}

void OTAHttpManager::register_routes(http::HttpServer::ptr server) {
    auto sd = server->getServletDispatch();

    // 注册 /ota/notify 路由
    sd->addServlet("/ota/notify", [this](http::HttpRequest::ptr req,
                                         http::HttpResponse::ptr rsp,
                                         http::HttpSession::ptr session) {
        return this->handle_notify(req, rsp, session);
    });

    // 注册 /ota/query 路由
    sd->addServlet("/ota/query", [this](http::HttpRequest::ptr req,
                                        http::HttpResponse::ptr rsp,
                                        http::HttpSession::ptr session) {
        return this->handle_query(req, rsp, session);
    });

    // 注册 /ota/query_download 路由
    sd->addServlet("/ota/query_download", [this](http::HttpRequest::ptr req,
                                                 http::HttpResponse::ptr rsp,
                                                 http::HttpSession::ptr session) {
        return this->handle_query_download(req, rsp, session);
    });

    // 注册 /ota/file_download/* 路由
    sd->addGlobServlet("/ota/file_download/*", [this](http::HttpRequest::ptr req,
                                                      http::HttpResponse::ptr rsp,
                                                      http::HttpSession::ptr session) {
        return this->handle_file_download(req, rsp, session);
    });

    // 注册 /ota/ 路由（OTA 页面）
    sd->addServlet("/ota/", [this](http::HttpRequest::ptr req,
                                   http::HttpResponse::ptr rsp,
                                   http::HttpSession::ptr session) {
        return this->handle_ota_page(req, rsp, session);
    });

    SYLAR_LOG_INFO(g_logger) << TAG << " all routes registered";
}

void OTAHttpManager::set_ota_html(const std::string& html) {
    m_ota_html_content = html;
    SYLAR_LOG_INFO(g_logger) << TAG << " OTA HTML content updated, size=" << html.size();
}

int OTAHttpManager::handle_notify(http::HttpRequest::ptr req,
                                  http::HttpResponse::ptr rsp,
                                  http::HttpSession::ptr session) {
    if (req->getMethod() == http::HttpMethod::OPTIONS) {
        set_options(rsp);
        return 0;
    }

    const std::string req_body = req->getBody();
    SYLAR_LOG_INFO(g_logger) << TAG << " notify request: " << req_body;

    try {
        nlohmann::json j = nlohmann::json::parse(req_body);
        if (!j.contains("device_type") || !j.contains("version") || !j.contains("name")) {
            rsp->setBody("request error: missing required fields");
            rsp->setStatus(http::HttpStatus::BAD_REQUEST);
            return 0;
        }
        SYLAR_LOG_DEBUG(g_logger) << TAG
            << "start ota nofify.";
        return ota_notify(j["device_type"], j["name"], j["version"], req_body, rsp);

    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG << " notify error: " << e.what();
        rsp->setBody("request error: invalid JSON");
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        return 0;
    }

    return 1;
}

int OTAHttpManager::ota_notify(uint16_t device_type
                            , const std::string& name
                            , const std::string& version
                            , const std::string& command
                            , http::HttpResponse::ptr rsp){
    SYLAR_LOG_DEBUG(g_logger) << TAG
        << "ota notify command " << command;
    // 1. get notify message
    struct OTAMessage msg;
    if(!OTANotifier::get_notify_message(device_type, name, version, msg)){
        
        std::stringstream ss;
        ss << "device_type = " << device_type
                                << ", name = " << name
                                << ", version = " << version
                                << " get notify message error";
        
        std::string sstr = ss.str();
        SYLAR_LOG_WARN(g_logger) << sstr;
        
        (void)setHttpResponse(rsp, http::HttpStatus::NOT_FOUND
                              , "get notify message error");
        return 0;
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

        return 0;
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
        return 1;
    }

    // 4. push command to message queue
    auto key = OTAHash::get_message_queue_hash();
    return redis_push_message_queue_by_http(m_redis_pool_name, key, command, rsp);
}

int OTAHttpManager::handle_stop_notify(http::HttpRequest::ptr req
                                       , http::HttpResponse::ptr rsp
                                       , http::HttpSession::ptr session) {
    if (req->getMethod() == http::HttpMethod::OPTIONS) {
        set_options(rsp);
        return 0;
    }

    const std::string req_body = req->getBody();
    SYLAR_LOG_INFO(g_logger) << TAG << "stop notify request: " << req_body;

    try {
        nlohmann::json j = nlohmann::json::parse(req_body);
        if (!j.contains("device_type") || !j.contains("version") || !j.contains("name")) {
            rsp->setBody("request error: missing required fields");
            rsp->setStatus(http::HttpStatus::BAD_REQUEST);
            return 0;
        }
        SYLAR_LOG_DEBUG(g_logger) << TAG
            << "start ota nofify.";
        return ota_stop_notify(j["device_type"], j["name"], j["version"], req_body, rsp);

    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG << " notify error: " << e.what();
        rsp->setBody("request error: invalid JSON");
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        return 0;
    }

    return 1;
}

int OTAHttpManager::ota_stop_notify(uint16_t device_type
                                , const std::string& name
                                , const std::string& version
                                , const std::string& command
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
        return 0;
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
            return 0;
        }

        // 4. push command to message queue
        auto key = OTAHash::get_message_queue_hash();
        return redis_push_message_queue_by_http(m_redis_pool_name, key, command, rsp);
    }
    (void)setHttpResponse(rsp, http::HttpStatus::INTERNAL_SERVER_ERROR,
                                 "unknown error.");
    return 0;
}

int OTAHttpManager::handle_query(http::HttpRequest::ptr req,
                                 http::HttpResponse::ptr rsp,
                                 http::HttpSession::ptr session) {
    if (req->getMethod() == http::HttpMethod::OPTIONS) {
        set_options(rsp);
        return 0;
    }

    const std::string req_body = req->getBody();
    SYLAR_LOG_INFO(g_logger) << TAG << " query request: " << req_body;

    try {
        nlohmann::json j = nlohmann::json::parse(req_body);
        if (!j.contains("device_type") || !j.contains("name")) {
            rsp->setBody("request error: missing required fields");
            rsp->setStatus(http::HttpStatus::BAD_REQUEST);
            return 0;
        }

        uint16_t device_type = j["device_type"];
        std::string name = j["name"];

        auto key = OTAHash::get_device_group_mudule_hash("query", device_type, name);
        auto ret = redis_query_by_http(m_redis_pool_name, key, rsp);
        if (ret == -1) {
            rsp->setBody("device not exists.");
        }
        rsp->setHeader("Content-Type", "application/json");

        return ret;

    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG << " query error: " << e.what();
        rsp->setBody("request error: invalid JSON");
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        return 0;
    }
}

int OTAHttpManager::handle_query_download(http::HttpRequest::ptr req,
                                          http::HttpResponse::ptr rsp,
                                          http::HttpSession::ptr session) {
    if (req->getMethod() == http::HttpMethod::OPTIONS) {
        set_options(rsp);
        return 0;
    }

    const std::string req_body = req->getBody();
    SYLAR_LOG_INFO(g_logger) << TAG << " query_download request: " << req_body;

    try {
        nlohmann::json j = nlohmann::json::parse(req_body);
        if (!j.contains("device_type") || !j.contains("name")) {
            rsp->setBody("request error: missing required fields");
            rsp->setStatus(http::HttpStatus::BAD_REQUEST);
            return 0;
        }

        uint16_t device_type = j["device_type"];
        std::string name = j["name"];

        auto key = OTAHash::get_device_group_mudule_hash("query_download", device_type, name);
        auto ret = redis_query_by_http(m_redis_pool_name, key, rsp);
        if (ret == -1) {
            rsp->setBody("device is not downloading.");
        }
        return ret;

    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG << " query_download error: " << e.what();
        rsp->setBody("request error: invalid JSON");
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        return 0;
    }
}

int OTAHttpManager::handle_file_download(http::HttpRequest::ptr req,
                                         http::HttpResponse::ptr rsp,
                                         http::HttpSession::ptr session) {
    if (req->getMethod() == http::HttpMethod::OPTIONS) {
        set_options(rsp);
        return 0;
    }

    std::string uri = req->getPath();

    // 解析 URI: /ota/file_download/{device_type}/{name}/{version}
    int idx = 19, start = 19;  // "/ota/file_download/" 长度为 19
    while (idx < (int)uri.size() && uri[idx] != '/') {
        ++idx;
    }

    if (idx >= (int)uri.size()) {
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        rsp->setBody("request error: invalid URI format");
        return 0;
    }
    uint16_t device_type = std::stoi(uri.substr(start, idx - start));

    ++idx;
    start = idx;
    while (idx < (int)uri.size() && uri[idx] != '/') {
        ++idx;
    }

    if (idx >= (int)uri.size()) {
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        rsp->setBody("request error: invalid URI format");
        return 0;
    }
    std::string name = uri.substr(start, idx - start);

    ++idx;
    start = idx;
    while (idx < (int)uri.size() && uri[idx] != '/') {
        ++idx;
    }

    std::string version = uri.substr(start, idx - start);
    if (idx != (int)uri.size()) {
        rsp->setStatus(http::HttpStatus::BAD_REQUEST);
        rsp->setBody("request error: invalid URI format");
        return 0;
    }

    SYLAR_LOG_INFO(g_logger) << TAG << " file_download: device_type=" << device_type
                             << ", name=" << name << ", version=" << version;

    ota_file_download(device_type, name, version, rsp, session);

    return 0;
}

int OTAHttpManager::handle_ota_page(http::HttpRequest::ptr req,
                                    http::HttpResponse::ptr rsp,
                                    http::HttpSession::ptr session) {
    if (req->getMethod() == http::HttpMethod::OPTIONS) {
        set_options(rsp);
        return 0;
    }

    if (m_ota_html_content.empty()) {
        rsp->setStatus(http::HttpStatus::NOT_FOUND);
        rsp->setBody("OTA page not available");
        return 0;
    }

    rsp->setStatus(http::HttpStatus::OK);
    rsp->setHeader("Content-Type", "text/html");
    rsp->setBody(m_ota_html_content);

    return 0;
}

void OTAHttpManager::set_options(http::HttpResponse::ptr rsp) {
    SYLAR_LOG_INFO(g_logger) << TAG << " OPTIONS request";
    rsp->setStatus(http::HttpStatus::OK);
    rsp->setHeader("Access-Control-Allow-Origin", "*");
    rsp->setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    rsp->setHeader("Access-Control-Allow-Headers", "Content-Type");
}

void OTAHttpManager::set_server_error(http::HttpResponse::ptr rsp) {
    SYLAR_LOG_ERROR(g_logger) << TAG << " server error!";
    rsp->setBody("server error");
    rsp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
}

void OTAHttpManager::ota_file_download(uint16_t device_type
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
    ioMgr->addEvent(sockfd, IOManager::Event::WRITE, [this, fd, thisFiber, file_size, offset, session, rsp](){
        while(rsp->isSending()){}
        rsp->setIsSending(false);
        sendFile(fd, thisFiber, offset, file_size, session);
    });

}

int OTAHttpManager::send_file(int fd, off_t* offset, size_t file_size, http::HttpSession::ptr session){
    return session->sendfile(fd, offset, file_size);
}

void OTAHttpManager::sendFile(int fd, Fiber::ptr thisFiber, off_t offset, size_t file_size, http::HttpSession::ptr session){

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
    IOManager::GetThis()->addEvent(sockfd, IOManager::Event::WRITE, [this, fd, thisFiber, file_size, offset, session](){
        sendFile(fd, thisFiber, offset, file_size, session);
    });

}

int OTAHttpManager::getFileDetail(const std::string& file_path, struct FileDetail& file_detail){

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

}
