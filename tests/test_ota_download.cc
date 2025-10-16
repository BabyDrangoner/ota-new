#include "../sherry/ota_subscribe_download.h"
#include "../sherry/ota_client_callback.h"
#include "../sherry/mqtt_client.h"
#include "../sherry/ota_client_callback.h"
#include "../sherry/log.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

std::string mqtt_client_id = "test subscribe download";
std::string mqtt_server_host = "localhost";
std::string protocol = "tcp";
int port = 1883;

std::string sub_topic = "/rtg/282/ota/agsspds";
int qos = 1;
int device_type = 0;

sherry::OTAClientCallbackManager::ptr cb_mgr = std::make_shared<sherry::OTAClientCallbackManager>();

void test01(){
    sherry::MqttAddress::ptr addr = std::make_shared<sherry::MqttAddress>(protocol, port, mqtt_server_host);
    sherry::MqttClientManager::ptr cli_mgr = std::make_shared<sherry::MqttClientManager>(port, protocol, mqtt_server_host);

    sherry::OTAMessage::ptr msg = std::make_shared<sherry::OTAMessage>();
    cb_mgr->regist_callback(sub_topic, [msg, ](const std::string& topic, const std::string& payload){
        msg->set_downloadMsg(payload);
        client->unsubscribe(topic);
    });

    sherry::OTASubscribeDownload::ptr ota_sd = std::make_shared<sherry::OTASubscribeDownload>(sub_topic, device_type, 0, cli_mgr, cb_mgr, msg);

    ota_sd->subscribe_download(sub_topic, qos);

    while(1){
        sleep(1);
        std::string download_msg = msg->get_downloadMsg();
        if(download_msg == "") continue;
        SYLAR_LOG_INFO(g_logger) << "subscribe to topic : " << sub_topic
                                << " for download details : " << download_msg
                                << std::endl;
    }
   
}

int main(){
    test01();
    return 0;
}