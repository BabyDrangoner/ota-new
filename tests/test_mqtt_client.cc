#include <iostream>
#include <mqtt/async_client.h>
#include <chrono>
#include <thread>

const std::string SERVER_ADDRESS("tcp://localhost:1883");
const std::string CLIENT_ID("paho_cpp_async");
const std::string TOPIC("test/topic");

int main(int argc, char* argv[]) {
    std::cout << "Creating MQTT client..." << std::endl;
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        std::cout << "Connecting..." << std::endl;
        auto tok = client.connect(connOpts);
        tok->wait();
        std::cout << "Connected!" << std::endl;
        
        // 给连接一些稳定时间
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::cout << "Subscribing..." << std::endl;
        client.subscribe(TOPIC, 1)->wait();
        std::cout << "Subscribed!" << std::endl;

        std::cout << "Publishing..." << std::endl;
        mqtt::message_ptr msg = mqtt::make_message(TOPIC, "Hello from paho mqtt cpp!");
        client.publish(msg)->wait();
        std::cout << "Published!" << std::endl;
        
        // 给消息一些处理时间
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::cout << "Disconnecting..." << std::endl;
        client.disconnect()->wait();
        std::cout << "Disconnected!" << std::endl;
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Error: " << exc.what() << std::endl;
        return 1;
    }

    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}