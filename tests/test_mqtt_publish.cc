#include "../sherry/sherry.h"
#include "../sherry/mqtt_client.h"
#include "../sherry/address.h"
#include "../sherry/iomanager.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

std::string getCurrentTimeString() {
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    // 转换为 time_t
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    // 转换为 tm 结构（本地时间）
    std::tm local_tm = *std::localtime(&now_time_t);

    // 使用 stringstream 格式化时间
    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
sherry::OTAClientCallbackManager::ptr cb_mgr = std::make_shared<sherry::OTAClientCallbackManager>();

std::string mqtt_client_id = "sherry publish";
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

std::string topic1 = "/ota/agsspds1/responder/";
std::string topic2 = "/ota/agsspds2/responder/";

void test01(){
    sherry::MqttAddress::ptr addr = std::make_shared<sherry::MqttAddress>(protocol, port, mqtt_server_host);
    sherry::MqttClient::ptr client = std::make_shared<sherry::MqttClient>(protocol, port, mqtt_server_host, mqtt_client_id, device_type, cb_mgr);
    
    client->connect();

    std::string payload = getCurrentTimeString();
    int i = 0;
    while(i++ < 5){
        if(i != 4){
            client->publish(topic1, payload, 1, true);
            client->publish(topic2, payload, 1, true);
        } else {
            client->publish(topic1, "123", 1, true);
            client->publish(topic2, "456", 1, true);

        }
        sleep(1);
    }
    
}

int main(void){
    test01();
    return 0;
}