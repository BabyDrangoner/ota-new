#include "../sherry/sherry.h"
#include "../sherry/mqtt_client.h"
#include "../sherry/address.h"
#include "../sherry/iomanager.h"
#include "../sherry/ota_client_callback.h"


static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
sherry::OTAClientCallbackManager::ptr cb_mgr = std::make_shared<sherry::OTAClientCallbackManager>();

std::string mqtt_client_id = "sherry subscribe";
std::string mqtt_server_host = "localhost";
std::string protocol = "tcp";
int port = 1883;

int device_type = 0;
// 声明一个 std::function 回调函数变量
std::function<void(const std::string&, const std::string&)> print_message_callback =
    [](const std::string& topic, const std::string& payload) {
        SYLAR_LOG_INFO(g_logger) << "subscribe topic: " << topic
                                 << " message: " << payload
                                 << std::endl;
};

std::string topic = "/ota/agsspds/query/";

void test01(){

    cb_mgr->regist_callback(topic, print_message_callback);

    sherry::MqttAddress::ptr addr = std::make_shared<sherry::MqttAddress>(protocol, port, mqtt_server_host);
    sherry::MqttClient::ptr client = std::make_shared<sherry::MqttClient>(protocol, port, mqtt_server_host, mqtt_client_id, device_type, cb_mgr);
    
    client->connect();
    client->subscribe(topic, 1);
    while(true);
}

int main(void){
    sherry::Scheduler sc(2, true, "test");
    sc.start();
    sc.schedule(&test01);
    sc.stop();

    return 0;
}