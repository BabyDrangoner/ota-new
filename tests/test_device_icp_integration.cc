/**
 * @file test_device_icp_integration.cc
 * @brief Device 模块与 ICP 模块联调测试
 * 
 * 测试场景:
 * 1. 启动 IcpServer
 * 2. DeviceEngine 连接到 IcpServer
 * 3. DeviceEngine 发送 ICP 格式消息
 * 4. IcpServer 接收并处理消息
 * 5. IcpServer 发送响应
 * 6. DeviceEngine 接收响应
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "sherry/log.h"
#include "sherry/iomanager.h"
#include "sherry/icp/icp.h"
#include "sherry/device/device_engine.h"
#include "sherry/device/device_camera.h"

using namespace sherry;
using namespace sherry::device;

// 使用完全限定名称避免冲突
namespace icp = sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("test");
static const char* TAG = "DeviceIcpIntegration";

// 测试结果收集
static std::atomic<int> g_messages_received{0};
static std::atomic<int> g_responses_sent{0};
static std::mutex g_mutex;
static std::condition_variable g_cv;

/**
 * @brief 测试1: 基本连接和消息发送
 */
void test_basic_connection() {
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Basic Connection ===";
    
    // 重置计数器
    g_messages_received.store(0);
    g_responses_sent.store(0);
    
    // 1. 创建配置
    auto config = std::make_shared<icp::IcpConfig>();
    config->server.bind_port = 19001;  // 使用不同端口避免冲突
    config->max_cars = 100;
    config->io_threads = 2;
    config->control_threads = 1;
    config->vllm.endpoint = "http://127.0.0.1:8000";
    config->min_submit_interval_ms = 100;
    
    // 2. 创建 IcpService
    auto service = std::make_shared<icp::IcpService>(config);
    
    if (!service->init()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to init IcpService";
        return;
    }
    
    // 设置自定义处理回调（不实际调用 vLLM）
    // 直接在收到消息后发送模拟响应
    
    if (!service->start()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to start IcpService";
        return;
    }
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] IcpServer started on port " << config->server.bind_port;
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 3. 创建 DeviceEngine
    DeviceEngine::Options opt;
    opt.server_ip = "127.0.0.1";
    opt.server_port = config->server.bind_port;
    opt.car_id = 42;
    opt.send_interval_ms = 500;  // 每500ms发送一次
    opt.connect_timeout_ms = 3000;
    
    // 创建模拟相机 (1张RGB图片, 无深度图)
    auto camera = std::make_shared<SingleCamera>(1, 0);
    
    auto engine = std::make_shared<DeviceEngine>(opt, camera);
    
    // 设置响应回调
    std::atomic<bool> response_received{false};
    engine->setServerMessageCallback([&](const std::string& msg) {
        SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] DeviceEngine received response: " << msg;
        response_received.store(true);
        g_cv.notify_all();
    });
    
    // 4. 启动 DeviceEngine
    if (!engine->start()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to start DeviceEngine";
        service->stop();
        return;
    }
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] DeviceEngine started, car_id=" << opt.car_id;
    
    // 5. 等待设备发送消息(至少 2 个消息周期)
    fprintf(stderr, "[DEBUG] before sleep\n");
    ::usleep(2000000);  // 2 seconds
    fprintf(stderr, "[DEBUG] after sleep\n");
    
    fprintf(stderr, "[DEBUG] begin verification\n");
    
    // 6. 验证 ICP 服务端确实收到并解析了消息
    bool passed = true;
    
    // 检查 RxSlot 是否收到了数据
    auto* rxSlotMgr = service->getController()->getRxSlotManager();
    auto* slot = rxSlotMgr->getSlot(opt.car_id);
    if (!slot || !slot->isValid()) {
        fprintf(stderr, "[DEBUG] RxSlot empty\n");
        passed = false;
    } else {
        uint64_t server_seq = slot->getCurrentSeq();
        fprintf(stderr, "[DEBUG] RxSlot seq=%lu\n", server_seq);
        if (server_seq == 0) {
            passed = false;
        }
        
        // 从 RxSlot 读出数据，验证可以被 MessageParser 完整解析
        std::vector<uint8_t> data;
        uint64_t out_seq, out_ts;
        if (slot->tryRead(data, out_seq, out_ts)) {
            icp::ParsedMessage parsed;
            auto pr = icp::MessageParser::parseMessage(data.data(), data.size(), parsed);
            if (pr != icp::MessageParser::ParseResult::OK) {
                fprintf(stderr, "[DEBUG] parse failed\n");
                passed = false;
            } else {
                fprintf(stderr, "[DEBUG] parsed OK car_id=%u images=%zu\n",
                        parsed.car_id, parsed.images.size());
                if (parsed.car_id != (uint32_t)opt.car_id) {
                    passed = false;
                }
            }
        } else {
            fprintf(stderr, "[DEBUG] tryRead failed\n");
            passed = false;
        }
    }
    
    // 检查服务端连接数
    size_t conn_count = service->getServer()->getConnectionCount();
    fprintf(stderr, "[DEBUG] connection count=%zu\n", conn_count);
    
    fprintf(stderr, "[DEBUG] before engine->stop()\n");
    // 7. 清理
    engine->stop();
    fprintf(stderr, "[DEBUG] after engine->stop()\n");
    
    service->stop();
    fprintf(stderr, "[DEBUG] after service->stop()\n");
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Basic Connection "
                              << (passed ? "PASSED" : "FAILED") << " ===";
}

/**
 * @brief 测试2: 多设备并发连接
 */
void test_multiple_devices() {
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Multiple Devices ===";
    
    // 1. 创建配置
    auto config = std::make_shared<icp::IcpConfig>();
    config->server.bind_port = 19002;
    config->max_cars = 100;
    config->io_threads = 2;
    config->control_threads = 1;
    config->vllm.endpoint = "http://127.0.0.1:8000";
    
    // 2. 创建 IcpService
    auto service = std::make_shared<icp::IcpService>(config);
    
    if (!service->init() || !service->start()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to start IcpService";
        return;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 3. 创建多个 DeviceEngine
    const int NUM_DEVICES = 3;
    std::vector<DeviceEngine::ptr> engines;
    std::atomic<int> connected_count{0};
    
    for (int i = 0; i < NUM_DEVICES; ++i) {
        DeviceEngine::Options opt;
        opt.server_ip = "127.0.0.1";
        opt.server_port = config->server.bind_port;
        opt.car_id = 100 + i;
        opt.send_interval_ms = 500;
        
        auto camera = std::make_shared<SingleCamera>(1, 0);
        auto engine = std::make_shared<DeviceEngine>(opt, camera);
        
        if (engine->start()) {
            engines.push_back(engine);
            connected_count++;
            SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Device " << i << " (car_id=" 
                                      << opt.car_id << ") connected";
        }
    }
    
    // 等待一段时间让消息交互
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 检查服务端连接数（此时 session 已注册）
    size_t server_connections = service->getServer()->getConnectionCount();
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Server has " << server_connections << " connections";
    
    // 停止所有设备
    for (auto& engine : engines) {
        engine->stop();
    }
    engines.clear();
    
    service->stop();
    
    // 验证: 客户端全部连接成功 + 服务端连接数一致
    bool passed = (connected_count.load() == NUM_DEVICES) &&
                  (server_connections == static_cast<size_t>(NUM_DEVICES));
    if (server_connections != static_cast<size_t>(NUM_DEVICES)) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Expected " << NUM_DEVICES 
                                  << " server connections, got " << server_connections;
    }
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Multiple Devices " 
                              << (passed ? "PASSED" : "FAILED") << " ===";
}

/**
 * @brief 测试3: 真实图片发送
 */
void test_real_image_send() {
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Real Image Send ===";
    
    // 1. 创建配置
    auto config = std::make_shared<icp::IcpConfig>();
    config->server.bind_port = 19003;
    config->max_cars = 100;
    config->io_threads = 2;
    config->control_threads = 1;
    
    // 2. 创建 IcpService
    auto service = std::make_shared<icp::IcpService>(config);
    
    if (!service->init() || !service->start()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to start IcpService";
        return;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 3. 创建带真实图片的 DeviceEngine
    DeviceEngine::Options opt;
    opt.server_ip = "127.0.0.1";
    opt.server_port = config->server.bind_port;
    opt.car_id = 200;
    opt.send_interval_ms = 300;
    
    // 尝试加载真实图片（如果存在）
    auto camera = std::make_shared<SingleCamera>(
        2, 1,  // 2张RGB + 1张深度图
        "",    // RGB 图片路径（空表示使用空数据）
        ""     // 深度图路径
    );
    
    auto engine = std::make_shared<DeviceEngine>(opt, camera);
    
    std::atomic<int> msg_count{0};
    engine->setServerMessageCallback([&](const std::string& msg) {
        msg_count++;
        SYLAR_LOG_DEBUG(g_logger) << "[" << TAG << "] Received response #" << msg_count;
    });
    
    if (!engine->start()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to start DeviceEngine";
        service->stop();
        return;
    }
    
    // 等待几个消息周期
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    // 验证服务端收到了消息并可完整解析
    bool passed = true;
    auto* rxSlotMgr = service->getController()->getRxSlotManager();
    auto* slot = rxSlotMgr->getSlot(opt.car_id);
    if (!slot || !slot->isValid()) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] RxSlot for car_id=" << opt.car_id << " is empty";
        passed = false;
    } else {
        std::vector<uint8_t> data;
        uint64_t out_seq, out_ts;
        if (slot->tryRead(data, out_seq, out_ts)) {
            icp::ParsedMessage parsed;
            auto pr = icp::MessageParser::parseMessage(data.data(), data.size(), parsed);
            if (pr != icp::MessageParser::ParseResult::OK) {
                SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Server failed to parse image message";
                passed = false;
            } else {
                SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Server parsed: car_id=" << parsed.car_id
                                          << " images=" << parsed.images.size();
                // 验证图片数量 = 2 RGB + 1 DEPTH = 3
                if (parsed.images.size() != 3) {
                    SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Expected 3 images, got " << parsed.images.size();
                    passed = false;
                }
            }
        }
    }
    
    engine->stop();
    service->stop();
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Sent messages, received " << msg_count << " responses";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Real Image Send "
                              << (passed ? "PASSED" : "FAILED") << " ===";
}

/**
 * @brief 测试4: 协议验证 - 验证 ICP 消息格式
 */
void test_protocol_verification() {
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Protocol Verification ===";
    
    // 使用 MessageBuilder 构建消息并验证
    icp::MessageBuilder builder;
    builder.setCarId(42);
    builder.setSeq(1);
    builder.setTimestamp(1234567890);
    
    // 添加一些测试图片数据
    std::vector<uint8_t> test_image = {0x89, 0x50, 0x4E, 0x47};  // PNG header
    builder.addImage(icp::ImageType::RGB, test_image.data(), test_image.size());
    
    std::vector<uint8_t> message = builder.build();
    
    // 验证消息头
    if (message.size() < icp::MessageHeader::SIZE) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Message too small: " << message.size();
        return;
    }
    
    icp::MessageHeader header;
    auto result = icp::MessageParser::parseHeader(message.data(), message.size(), header);
    
    if (result != icp::MessageParser::ParseResult::OK) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to parse header";
        return;
    }
    
    bool passed = true;
    
    if (header.magic != icp::MessageHeader::MAGIC) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Invalid magic: " << std::hex 
                                   << header.magic << " expected " << icp::MessageHeader::MAGIC;
        passed = false;
    }
    
    if (header.car_id != 42) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Invalid car_id: " << header.car_id;
        passed = false;
    }
    
    if (header.seq != 1) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Invalid seq: " << header.seq;
        passed = false;
    }
    
    if (header.image_nums != 1) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Invalid image_nums: " << header.image_nums;
        passed = false;
    }
    
    // 完整解析
    icp::ParsedMessage parsed;
    result = icp::MessageParser::parseMessage(message.data(), message.size(), parsed);
    
    if (result != icp::MessageParser::ParseResult::OK) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Failed to parse full message";
        passed = false;
    } else {
        if (parsed.images.size() != 1) {
            SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Wrong image count: " << parsed.images.size();
            passed = false;
        } else if (parsed.images[0].size != test_image.size()) {
            SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] Wrong image size: " << parsed.images[0].size;
            passed = false;
        }
    }
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Message size: " << message.size() 
                              << " bytes, header.message_size: " << header.message_size;
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test Protocol Verification " 
                              << (passed ? "PASSED" : "FAILED") << " ===";
}

/**
 * @brief 测试5: ICP 结果回调
 */
void test_icp_result_callback() {
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test ICP Result Callback ===";
    
    // 测试 OutputMessage 序列化/反序列化
    icp::OutputMessage original;
    original.car_id = 42;
    original.seq = 100;
    original.status = "success";
    original.latency_ms = 125;
    original.waypoints = R"({"points":[[1,2],[3,4]]})";
    
    std::string json = original.toJson();
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Serialized: " << json;
    
    icp::OutputMessage parsed_out = icp::OutputMessage::fromJson(json);
    
    bool passed = true;
    if (parsed_out.car_id != original.car_id) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] car_id mismatch";
        passed = false;
    }
    if (parsed_out.seq != original.seq) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] seq mismatch";
        passed = false;
    }
    if (parsed_out.status != original.status) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] status mismatch: " << parsed_out.status;
        passed = false;
    }
    if (parsed_out.latency_ms != original.latency_ms) {
        SYLAR_LOG_ERROR(g_logger) << "[" << TAG << "] latency_ms mismatch";
        passed = false;
    }
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] === Test ICP Result Callback " 
                              << (passed ? "PASSED" : "FAILED") << " ===";
}

int main(int argc, char* argv[]) {
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ========================================";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] Device-ICP Integration Tests";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ========================================";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ";
    
    // 运行测试
    test_protocol_verification();
    test_icp_result_callback();
    test_basic_connection();
    test_multiple_devices();
    test_real_image_send();
    
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ========================================";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] All Integration Tests Completed";
    SYLAR_LOG_INFO(g_logger) << "[" << TAG << "] ========================================";
    
    return 0;
}
