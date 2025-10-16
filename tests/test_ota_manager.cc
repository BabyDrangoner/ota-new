#include "../sherry/sherry.h"
#include "../sherry/ota_manager.h"
#include "../sherry/log.h"
#include "../sherry/http/http_send_buffer.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");


std::string protocol = "tcp";
std::string host = "localhost";
int port = 1883;
size_t file_size = 4096;

uint16_t device_type = 1;

void test_ota_manager_notifier(){
    sherry::OTAManager::ptr ota_mgr = std::make_shared<sherry::OTAManager>(file_size, protocol, host, port, 1.1, "../ota/file");
    sherry::OTAMessage msg;
    msg.name = "agsspds";
    msg.version = "5.1.2.0";
    msg.time = sherry::getCurrentTimeString();
    msg.file_name = "agsspds_20241110.zip";
    msg.file_size = 6773120;
    msg.url_path = "http://127.0.0.1:18882/download/ota/agsspds";
    msg.md5_value = "ed076287532e86365e841e92bfc50d8c";
    msg.launch_mode = 0;
    msg.upgrade_mode = 1;

    ota_mgr->add_device(device_type, 1);
    ota_mgr->ota_notify(device_type, msg, 1);

    int i = 0;
    while(true){
        if(i == 5){
            ota_mgr->ota_stop_notify(device_type, "asspid", "1.0.0.1", 1);
            break;
        }
        ++i;
        sleep(1);
    }
}

void test_ota_manager_query(){
    sherry::HttpSendBuffer::ptr http_send_buffer = std::make_shared<sherry::HttpSendBuffer>();    
    sherry::OTAManager::ptr ota_mgr = std::make_shared<sherry::OTAManager>(file_size, protocol, host, port, 1.1, "../file/");
    ota_mgr->add_device(device_type, 1);
    ota_mgr->ota_query(device_type, 1, "version", 0);

    while(1);

}

int main(){
    // test_ota_manager_notifier();
    test_ota_manager_query();
    return 0;
}


