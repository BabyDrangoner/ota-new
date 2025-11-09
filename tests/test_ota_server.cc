#include "../sherry/http/http_server.h"
#include "../sherry/log.h"
#include "../sherry/ota_manager.h"
#include "../sherry/ota_http_command_dispatcher.h"
#include "../sherry/fiber.h"
#include "../sherry/config.h"
#include "../sherry/db/redis.h"

#include <thread>
#include <fcntl.h>
#include <yaml-cpp/yaml.h>

static sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();

sherry::IOManager::ptr worker;

std::string protocol = "tcp";
std::string host = "localhost";
int port = 1883;
size_t file_size = 4096;

uint16_t device_type = 1;
sherry::OTAManager::ptr ota_mgr = nullptr;

std::string ota_html;
const std::string ota_htmlPath = "./file/ota.html";

// ---------------redis pool ---------------------
const std::string redisPool_name = "local";

void setOptions(sherry::http::HttpResponse::ptr rsp){
    SYLAR_LOG_INFO(g_logger) << "OPTIONS";

    rsp->setStatus(sherry::http::HttpStatus::OK); // 204
    rsp->setHeader("Access-Control-Allow-Origin", "*");
    rsp->setHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    rsp->setHeader("Access-Control-Allow-Headers", "Content-Type");
}

void setServerError(sherry::http::HttpResponse::ptr rsp){
    SYLAR_LOG_ERROR(g_logger) << "server error !";
    rsp->setBody("server error");
    rsp->setStatus(sherry::http::HttpStatus::OK);
}

void run(){
    sherry::http::HttpServer::ptr server(new sherry::http::HttpServer(true, worker.get(), sherry::IOManager::GetThis()));
    sherry::Address::ptr addr = sherry::Address::LookupAnyIPAddress("0.0.0.0:8020");
    ota_mgr->SetThis();
    ota_mgr->add_device(device_type, 1);

    while(!server->bind(addr)){
        sleep(2);
    }

    auto sd = server->getServletDispatch();
    sd->addServlet("/ota/notify", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
        if(req->getMethod() == sherry::http::HttpMethod::OPTIONS) {
            setOptions(rsp);
            return 0;
        }

        sherry::OTAManager* otaMgr = sherry::OTAManager::GetThis();
        if(!otaMgr){
            setServerError(rsp);
            return 0;
        }

        SYLAR_LOG_INFO(g_logger) << req->getBody();
        nlohmann::json j = nlohmann::json::parse(req->getBody());
        if(!j.contains("device_type") || !j.contains("version") || !j.contains("name")){
            rsp->setBody("request error");
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            return 0;
        }

        uint64_t device_type = j["device_type"];
        std::string name = j["name"];
        std::string version = j["version"];
    
        otaMgr->ota_notify(device_type, name, version, rsp);

        return 0;
    });

    sd->addServlet("/ota/query", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
       if(req->getMethod() == sherry::http::HttpMethod::OPTIONS) {
            setOptions(rsp);
            return 0;
        }

        sherry::OTAManager* otaMgr = sherry::OTAManager::GetThis();
        if(!otaMgr){
            setServerError(rsp);
            return 0;
        }

        SYLAR_LOG_INFO(g_logger) << req->getBody();
        nlohmann::json j = nlohmann::json::parse(req->getBody());
        if(!j.contains("device_type") || !j.contains("device_no") || !j.contains("action")){
            rsp->setBody("request error");
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            return 0;
        }

        uint16_t device_type = j["device_type"];
        uint32_t device_no = j["device_no"];
        std::string action = j["action"];
    
        otaMgr->ota_query(device_type, device_no, action, rsp);
        rsp->setHeader("Content-Type", "application/json");

        return 0;
    });

    sd->addServlet("/ota/query_download", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
       if(req->getMethod() == sherry::http::HttpMethod::OPTIONS) {
            setOptions(rsp);
            return 0;
        }

        sherry::OTAManager* otaMgr = sherry::OTAManager::GetThis();
        if(!otaMgr){
            setServerError(rsp);
            return 0;
        }

        SYLAR_LOG_INFO(g_logger) << req->getBody();
        nlohmann::json j = nlohmann::json::parse(req->getBody());
        if(!j.contains("device_type") || !j.contains("device_no") || !j.contains("name")){
            rsp->setBody("request error");
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            return 0;
        }

        uint16_t device_type = j["device_type"];
        uint32_t device_no = j["device_no"];
        std::string name = j["name"];
    
        otaMgr->ota_query_download(device_type, device_no, name, rsp);

        return 0;
    });

    sd->addGlobServlet("/ota/file_download/*", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
       if(req->getMethod() == sherry::http::HttpMethod::OPTIONS) {
            setOptions(rsp);
            return 0;
        }

        sherry::OTAManager* otaMgr = sherry::OTAManager::GetThis();
        if(!otaMgr){
            setServerError(rsp);
            return 0;
        }

        std::string uri = req->getPath();

        int idx = 19, start = 19;
        while(idx < (int)uri.size() && uri[idx] != '/'){
            ++idx;
        }

        if(idx >= (int)uri.size()){
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            rsp->setBody("request error.");
            return 0;
        }
        uint16_t device_type = std::stoi(uri.substr(start, idx - start));
        
        ++idx;
        start = idx;
        while(idx < (int)uri.size() && uri[idx] != '/'){
            ++idx;
        }

        if(idx >= (int)uri.size()){
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            rsp->setBody("request error.");
            return 0;
        }
        std::string name = uri.substr(start, idx - start);

        ++idx;
        start = idx;
        while(idx < (int)uri.size() && uri[idx] != '/'){
            ++idx;
        }

        std::string version = uri.substr(start, idx - start);
        if(idx != (int)uri.size()){
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            rsp->setBody("request error.");
            return 0;
        }
    
        otaMgr->ota_file_download(device_type, name, version, rsp, session);

        return 0;
    });

    sd->addServlet("/ota/", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
        if(req->getMethod() == sherry::http::HttpMethod::OPTIONS) {
            setOptions(rsp);
            return 0;
        }

        if(ota_html.size() == 0){
            
            rsp->setStatus(sherry::http::HttpStatus::NOT_FOUND);
            return 0;
        }

        rsp->setStatus(sherry::http::HttpStatus::OK);
        rsp->setHeader("Content-Type", "text/html");


        rsp->setBody(ota_html);

        return 0;
    });

    sd->addServlet("/sherry/xx", [](sherry::http::HttpRequest::ptr req
                                ,sherry::http::HttpResponse::ptr rsp
                                ,sherry::http::HttpSession::ptr session){
        rsp->setStatus(sherry::http::HttpStatus::OK);
        rsp->setBody("Glob");

        return 0;
    });


    server->start();
}

void monitor_fiber_count() {
    while (true) {
        sleep(2);  // 或使用定时器
        std::cout << "[Monitor] Fiber Count: " << sherry::Fiber::TotalFibers()
                  << ", Fiber id: " << sherry::Fiber::TotalUsedFibers()
                  << std::endl;
    }
}

int getOtaHtml(const char* filePath, char* file, size_t len){
    int fd = open(filePath, O_RDONLY);
    if(fd < 0){
        SYLAR_LOG_WARN(g_logger) << "open html error: " << strerror(errno);
        return -1;
    }

    ssize_t rt = read(fd, file, len);
    if(rt == -1){
        SYLAR_LOG_WARN(g_logger) << "read html error: " << strerror(errno);
        close(fd);
        return -1;
    }

    close(fd);
    return rt;  // 返回实际读取的长度更有意义
}


int main(int argc, char** argv){
    g_logger->setLevel(sherry::LogLevel::DEBUG);

    // 1. 加载配置文件
    try {
        YAML::Node config = YAML::LoadFile("./config/ota_system.yaml");
        sherry::Config::LoadFromYaml(config);
        SYLAR_LOG_INFO(g_logger) << "Config loaded successfully";
    } catch(const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to load config: " << e.what();
        // 配置加载失败，但继续运行（使用默认配置）
    }

    // 2. 在主线程中提前初始化 RedisManager（避免多线程竞态）
    sherry::RedisManager* redis_mgr = sherry::RedisMgr::GetInstance();
    redis_mgr->dump(std::cout);

    char buff[4096 * 2];
    
    int len = getOtaHtml(ota_htmlPath.c_str(), buff, sizeof(buff));
    ota_html = buff;
    
    SYLAR_LOG_DEBUG(g_logger) << "read html len = " << len;

    worker.reset(new sherry::IOManager(4, false, "worker"));

    // 3. 创建 OTAManager
    ota_mgr = std::make_shared<sherry::OTAManager>(file_size, protocol, host, port, "./file/", worker, 
                                                    redisPool_name);
    sherry::IOManager iom(1, true, "main");
    iom.schedule(run);

    // std::thread(monitor_fiber_count).detach();
    return 0;
}