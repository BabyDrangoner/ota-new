#include "../sherry/sherry.h"
#include "../sherry/mqtt_client.h"
#include "../sherry/address.h"
#include "../sherry/iomanager.h"
#include "../sherry/timer.h"
#include "../sherry/ota_notifier.h"
#include "../sherry/ota_client_callback.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

using namespace sherry;

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
sherry::OTAClientCallbackManager::ptr cb_mgr = std::make_shared<sherry::OTAClientCallbackManager>();

 // MQTT 参数
std::string protocol = "tcp";
std::string host = "localhost";
int port = 1883;
std::string client_id = "ota_notifier_test";
int device_type = 1;
std::string topic = "ota/test/device";
sherry::MqttClientManager::ptr cli_mgr = std::make_shared<sherry::MqttClientManager>(port, protocol, host, cb_mgr);

void test_ota_notifier() {
   

    // 创建 MQTT client
    // MqttClient::ptr client = cli_mgr->get_client(device_type);

    // client->connect();

    // 构造 TimerManager
    auto timer_mgr = std::make_shared<IOManager>(1, false, "OTA-Timer");

    // 构造 OTA 消息
    OTAMessage msg;
    msg.name = "agsspds";
    msg.version = "5.1.2.0";
    msg.time = getCurrentTimeString();
    msg.file_name = "agsspds_20241110.zip";
    msg.file_size = 6773120;
    msg.url_path = "http://127.0.0.1:18882/download/ota/agsspds";
    msg.md5_value = "ed076287532e86365e841e92bfc50d8c";
    msg.launch_mode = 0;
    msg.upgrade_mode = 1;

    // 创建 OTANotifier 并启动
    OTANotifier::ptr notifier = std::make_shared<OTANotifier>(
        device_type, timer_mgr, topic, cli_mgr, 1000  // 10 秒发布一次
    );
    notifier->set_message(msg);
    notifier->start();

    // 持续运行（可中断）
    while (true) {
        sleep(1);
    }
}

int main() {
    // Scheduler sc(2, true, "ota-test");
    // sc.start();
    // sc.schedule(&test_ota_notifier);
    // sc.stop();

    test_ota_notifier();
    return 0;
}
