#include "../sherry/http/http_server.h"
#include "../sherry/log.h"
#include "../sherry/ota_mqtt_manager.h"
#include "../sherry/ota_http_manager.h"
#include "../sherry/fiber.h"
#include "../sherry/config.h"
#include "../sherry/db/redis.h"
#include "../sherry/db/redis_util.h"
#include "../sherry/hash.h"

#include <thread>
#include <fcntl.h>
#include <yaml-cpp/yaml.h>

using namespace sherry;

static sherry::Logger::ptr g_logger = SYLAR_LOG_ROOT();

sherry::IOManager::ptr worker;

std::string protocol = "tcp";
std::string host = "localhost";
int port = 1883;
size_t file_size = 4096;

uint16_t device_type = 1;
sherry::OTAMqttManager::ptr ota_mgr = nullptr;
sherry::OTAHttpManager::ptr ota_http_mgr = nullptr;
sherry::OTAClientCallbackManager::ptr ota_cb_mgr = nullptr;

std::string ota_html;
const std::string ota_htmlPath = "./file/ota.html";

// ---------------redis pool ---------------------
static const std::string redis_ota_pool_name = "ota_pool";
static const std::string redis_http_pool_name = "http_pool";
static const std::string redis_mq_pool_name = "ota_mq_pool";

void run(){
    sherry::http::HttpServer::ptr server(new sherry::http::HttpServer(true, worker.get(), sherry::IOManager::GetThis()));
    sherry::Address::ptr addr = sherry::Address::LookupAnyIPAddress("0.0.0.0:8020");
    // ota_mgr->SetThis();
    // ota_mgr->add_device(device_type, 1);

    while(!server->bind(addr)){
        sleep(2);
    }


    ota_mgr->start();

    // 注册所有 OTA HTTP 路由
    ota_http_mgr->register_routes(server);

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

    // 1. 注册配置项（必须在加载 YAML 之前）
    auto mqtt_devices_config = sherry::Config::Lookup<std::vector<sherry::MqttDeviceConfig>>(
        "mqtt.devices", std::vector<sherry::MqttDeviceConfig>(), "mqtt devices config");

    // 2. 加载配置文件
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

    worker.reset(new sherry::IOManager(8, false, "worker"));

    // 3. 创建 OTAManager
    ota_cb_mgr = std::make_shared<OTAClientCallbackManager>();
    ota_mgr = std::make_shared<sherry::OTAMqttManager>(file_size, ota_cb_mgr,
                                                       redis_ota_pool_name, redis_mq_pool_name);
    
    // 4. 创建 OTAHttpManager
    ota_http_mgr = std::make_shared<sherry::OTAHttpManager>(redis_http_pool_name, "./file/", ota_html);
    
    sherry::IOManager iom(1, true, "main");
    iom.schedule(run);

    // std::thread(monitor_fiber_count).detach();
    return 0;
}