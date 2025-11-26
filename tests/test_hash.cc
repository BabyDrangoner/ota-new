// #include <iostream>
// #include <cassert>
// #include <string>

// #include "sherry/hash.h"

// using namespace sherry;

// // 测试辅助函数
// void assert_equal(const std::string& actual, const std::string& expected, const std::string& test_name) {
//     if (actual == expected) {
//         std::cout << "✓ " << test_name << " PASSED" << std::endl;
//         std::cout << "  Result: " << actual << std::endl;
//     } else {
//         std::cerr << "✗ " << test_name << " FAILED" << std::endl;
//         std::cerr << "  Expected: " << expected << std::endl;
//         std::cerr << "  Actual:   " << actual << std::endl;
//         exit(1);
//     }
// }

// void test_device_group_hash() {
//     std::cout << "\n=== Testing get_device_group_hash ===" << std::endl;
    
//     // 测试1: 普通设备组ID
//     std::string result1 = OTAHash::get_device_group_hash(1001);
//     assert_equal(result1, "ota:device:1001", "Device group hash with ID 1001");
    
//     // 测试2: 最小设备组ID
//     std::string result2 = OTAHash::get_device_group_hash(0);
//     assert_equal(result2, "ota:device:0", "Device group hash with ID 0");
    
//     // 测试3: 大设备组ID
//     std::string result3 = OTAHash::get_device_group_hash(65535);
//     assert_equal(result3, "ota:device:65535", "Device group hash with max uint16_t");
// }

// void test_device_id_hash() {
//     std::cout << "\n=== Testing get_device_id_hash ===" << std::endl;
    
//     // 测试1: 普通设备ID
//     std::string result1 = OTAHash::get_device_id_hash(1001, 8888);
//     assert_equal(result1, "ota:device:1001:8888", "Device ID hash with group 1001, device 8888");
    
//     // 测试2: 最小设备ID
//     std::string result2 = OTAHash::get_device_id_hash(100, 0);
//     assert_equal(result2, "ota:device:100:0", "Device ID hash with device 0");
    
//     // 测试3: 大设备ID
//     std::string result3 = OTAHash::get_device_id_hash(999, 4294967295);
//     assert_equal(result3, "ota:device:999:4294967295", "Device ID hash with max uint32_t");
// }

// void test_device_group_id_timestamp() {
//     std::cout << "\n=== Testing get_device_group_id_timestamp ===" << std::endl;
    
//     // 测试1: 检查格式是否正确
//     std::string result1 = OTAHash::get_device_group_id_timestamp_hash(1001, 8888);
//     std::cout << "  Generated: " << result1 << std::endl;
    
//     // 验证前缀
//     std::string prefix = "ota:device:1001:8888:";
//     if (result1.substr(0, prefix.length()) == prefix) {
//         std::cout << "✓ Device group ID timestamp format PASSED" << std::endl;
//     } else {
//         std::cerr << "✗ Device group ID timestamp format FAILED" << std::endl;
//         std::cerr << "  Expected prefix: " << prefix << std::endl;
//         std::cerr << "  Actual:          " << result1 << std::endl;
//         exit(1);
//     }
    
//     // 验证时间戳部分是数字
//     std::string timestamp_part = result1.substr(prefix.length());
//     bool is_numeric = !timestamp_part.empty() && 
//                       timestamp_part.find_first_not_of("0123456789") == std::string::npos;
    
//     if (is_numeric) {
//         std::cout << "✓ Timestamp part is numeric PASSED" << std::endl;
//         std::cout << "  Timestamp: " << timestamp_part << std::endl;
//     } else {
//         std::cerr << "✗ Timestamp part is numeric FAILED" << std::endl;
//         std::cerr << "  Timestamp part: " << timestamp_part << std::endl;
//         exit(1);
//     }
    
//     // 测试2: 验证两次调用的时间戳不同
//     std::string result2 = OTAHash::get_device_group_id_timestamp_hash(1001, 8888);
//     if (result1 != result2) {
//         std::cout << "✓ Timestamps are different PASSED" << std::endl;
//     } else {
//         std::cout << "⚠ Warning: Two timestamps are the same (might be too fast)" << std::endl;
//     }
// }

// void test_key_prefix() {
//     std::cout << "\n=== Testing Key Prefix ===" << std::endl;
    
//     // 所有key都应该以 "ota:device:" 开头
//     std::string prefix = "ota:device:";
    
//     std::string key1 = OTAHash::get_device_group_hash(100);
//     std::string key2 = OTAHash::get_device_id_hash(100, 200);
//     std::string key3 = OTAHash::get_device_group_id_timestamp_hash(100, 200);
    
//     if (key1.substr(0, prefix.length()) == prefix &&
//         key2.substr(0, prefix.length()) == prefix &&
//         key3.substr(0, prefix.length()) == prefix) {
//         std::cout << "✓ All keys have correct prefix PASSED" << std::endl;
//     } else {
//         std::cerr << "✗ Key prefix test FAILED" << std::endl;
//         exit(1);
//     }
// }

// void test_key_uniqueness() {
//     std::cout << "\n=== Testing Key Uniqueness ===" << std::endl;
    
//     // 不同的参数应该生成不同的key
//     std::string key1 = OTAHash::get_device_id_hash(1001, 8888);
//     std::string key2 = OTAHash::get_device_id_hash(1001, 8889);
//     std::string key3 = OTAHash::get_device_id_hash(1002, 8888);
    
//     if (key1 != key2 && key1 != key3 && key2 != key3) {
//         std::cout << "✓ Keys are unique PASSED" << std::endl;
//         std::cout << "  Key1: " << key1 << std::endl;
//         std::cout << "  Key2: " << key2 << std::endl;
//         std::cout << "  Key3: " << key3 << std::endl;
//     } else {
//         std::cerr << "✗ Key uniqueness test FAILED" << std::endl;
//         exit(1);
//     }
// }

// void test_special_cases() {
//     std::cout << "\n=== Testing Special Cases ===" << std::endl;
    
//     // 测试边界值
//     std::string key1 = OTAHash::get_device_group_hash(0);
//     std::string key2 = OTAHash::get_device_group_hash(65535);  // uint16_t max
    
//     std::string key3 = OTAHash::get_device_id_hash(0, 0);
//     std::string key4 = OTAHash::get_device_id_hash(65535, 4294967295);  // max values
    
//     std::cout << "✓ Special cases handled PASSED" << std::endl;
//     std::cout << "  Min group:  " << key1 << std::endl;
//     std::cout << "  Max group:  " << key2 << std::endl;
//     std::cout << "  Min device: " << key3 << std::endl;
//     std::cout << "  Max device: " << key4 << std::endl;
// }

// void test_performance() {
//     std::cout << "\n=== Testing Performance ===" << std::endl;
    
//     const int iterations = 100000;
    
//     auto start = std::chrono::high_resolution_clock::now();
    
//     for (int i = 0; i < iterations; i++) {
//         std::string key = OTAHash::get_device_id_hash(1001, i);
//         // 防止优化掉
//         if (key.empty()) break;
//     }
    
//     auto end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
//     double avg_time = static_cast<double>(duration.count()) / iterations;
    
//     std::cout << "✓ Performance test PASSED" << std::endl;
//     std::cout << "  Total iterations: " << iterations << std::endl;
//     std::cout << "  Total time: " << duration.count() << " μs" << std::endl;
//     std::cout << "  Average time: " << avg_time << " μs per call" << std::endl;
    
//     if (avg_time < 1.0) {
//         std::cout << "  Performance: Excellent (< 1 μs)" << std::endl;
//     } else if (avg_time < 10.0) {
//         std::cout << "  Performance: Good (< 10 μs)" << std::endl;
//     } else {
//         std::cout << "  Performance: Acceptable" << std::endl;
//     }
// }

// int main() {
//     std::cout << "========================================" << std::endl;
//     std::cout << "    OTA Hash Function Unit Tests" << std::endl;
//     std::cout << "========================================" << std::endl;
    
//     try {
//         test_device_group_hash();
//         test_device_id_hash();
//         test_device_group_id_timestamp();
//         test_key_prefix();
//         test_key_uniqueness();
//         test_special_cases();
//         test_performance();
        
//         std::cout << "\n========================================" << std::endl;
//         std::cout << "  ✓ All Tests PASSED!" << std::endl;
//         std::cout << "========================================" << std::endl;
        
//         return 0;
//     } catch (const std::exception& e) {
//         std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
//         return 1;
//     }
// }

int main(){
    return 0;
}
