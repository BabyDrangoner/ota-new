#include "../sherry/sherry.h"
#include "../sherry/ota_query_responder.h"
#include "../sherry/mqtt_client.h"
#include "../sherry/ota_client_callback.h"

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

std::string protocol = "tcp";
std::string host = "localhost";
int port = 1883;
std::string client_id = "ota_query_test";
int device_type = 1;
int device_no = 0;
std::string name = "agsspds";
std::string action = "getVersion";
std::string sub_topic1 = "/ota/agsspds1/responder/";
std::string sub_topic2 = "/ota/agsspds2/responder/";

sherry::OTAClientCallbackManager::ptr cb_mgr = std::make_shared<sherry::OTAClientCallbackManager>();
sherry::MqttClientManager::ptr cli_mgr = std::make_shared<sherry::MqttClientManager>(port, protocol, host, cb_mgr);

void test01(){

    std::string pub_topic = "/ota/agsspds/query/";
    sherry::OTAQueryResponder::ptr oqr = std::make_shared<sherry::OTAQueryResponder>(device_type, device_no, cli_mgr);

    cb_mgr->regist_callback(sub_topic1, [oqr, pub_topic](const std::string& topic, const std::string& payload){
        if(payload == "123"){
            oqr->subscribe_on_success(pub_topic, topic);
        }
    });

    cb_mgr->regist_callback(sub_topic2, [oqr, pub_topic](const std::string& topic, const std::string& payload){
        if(payload == "456"){
            oqr->subscribe_on_success(pub_topic, topic);
        }
    });
    
    oqr->publish_query(action, 1, true);
    sleep(1);
    oqr->subscribe_responder(pub_topic, sub_topic1, 1);
    oqr->subscribe_responder(pub_topic, sub_topic2, 1);


    while(1);
}

int main(void){

    test01();

    return 0;
}