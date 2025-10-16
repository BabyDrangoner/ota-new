#include "../sherry/http/http_send_buffer.h"  
#include <iostream>

using namespace sherry;

int main() {
    HttpSendBuffer buffer;

    std::string json1 = R"({
        "device_type": 1,
        "device_no": 101,
        "name": "main_board",
        "download_details": {"version": "1.0.0"},
        "query_details": {"status": "idle"}
    })";

    std::string json2 = R"({
        "device_type": 1,
        "device_no": 101,
        "name": "sensor_unit",
        "download_details": {"version": "2.0.0"},
        "query_details": {"status": "active"}
    })";

    std::string json3 = R"({
        "device_type": 2,
        "device_no": 202,
        "name": "core_ctrl",
        "download_details": {"version": "3.0.0"},
        "query_details": {"status": "updating"}
    })";

    // 添加数据
    buffer.addData(1, json1);
    buffer.addData(1, json2);
    buffer.addData(2, json3);

    // 获取数据（应包含两个设备信息）
    std::cout << "=== GetData for key 1 ===" << std::endl;
    std::string out1 = buffer.getData(1);
    std::cout << out1 << std::endl;

    // 获取后清空，再次获取应为空
    std::cout << "=== GetData for key 1 again (should be empty) ===" << std::endl;
    std::string out1_again = buffer.getData(1);
    std::cout << out1_again << std::endl;

    // 另一个 key 获取
    std::cout << "=== GetData for key 2 ===" << std::endl;
    std::string out2 = buffer.getData(2);
    std::cout << out2 << std::endl;

    return 0;
}
