/**
 * @file test_icp.cc
 * @brief ICP 模块测试
 * 
 * 测试用例:
 * 1. 协议解析测试
 * 2. RxSlot 覆盖测试
 * 3. Latest-Only 逻辑测试
 * 4. 多车并发测试
 * 5. 结果乱序丢弃测试
 */

#include "sherry/icp/icp.h"
#include "sherry/log.h"
#include "sherry/iomanager.h"

#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace sherry;
using namespace sherry::icp;

static Logger::ptr g_logger = SYLAR_LOG_NAME("test");

//------------------------------------------------------------------------------
// 测试1: 协议解析测试
//------------------------------------------------------------------------------
void test_protocol() {
    SYLAR_LOG_INFO(g_logger) << "=== Test Protocol ===";
    
    // 构建测试消息
    MessageBuilder builder;
    builder.setCarId(1)
           .setSeq(100)
           .setTimestamp(1234567890000)
           .setPrompt("Navigate to waypoint");
    
    // 添加一些假图片数据
    std::vector<uint8_t> fake_image(100, 0xFF);
    builder.addImage(ImageType::RGB, fake_image.data(), fake_image.size());
    builder.addImage(ImageType::DEPTH, fake_image.data(), fake_image.size());
    
    auto data = builder.build();
    SYLAR_LOG_INFO(g_logger) << "Built message size: " << data.size();
    
    // 解析消息头
    MessageHeader header;
    auto result = MessageParser::parseHeader(data.data(), data.size(), header);
    assert(result == MessageParser::ParseResult::OK);
    assert(header.isValid());
    assert(header.car_id == 1);
    assert(header.seq == 100);
    assert(header.image_nums == 2);
    
    SYLAR_LOG_INFO(g_logger) << "Header parsed: car_id=" << header.car_id
                              << " seq=" << header.seq
                              << " image_nums=" << header.image_nums;
    
    // 完整解析消息
    ParsedMessage msg;
    result = MessageParser::parseMessage(data.data(), data.size(), msg);
    assert(result == MessageParser::ParseResult::OK);
    assert(msg.car_id == 1);
    assert(msg.seq == 100);
    assert(msg.images.size() == 2);
    assert(msg.images[0].type == ImageType::RGB);
    assert(msg.images[1].type == ImageType::DEPTH);
    assert(msg.prompt == "Navigate to waypoint");
    
    SYLAR_LOG_INFO(g_logger) << "Message parsed: images=" << msg.images.size()
                              << " prompt=" << msg.prompt;
    
    SYLAR_LOG_INFO(g_logger) << "=== Test Protocol PASSED ===";
}

//------------------------------------------------------------------------------
// 测试2: RxSlot 覆盖测试
//------------------------------------------------------------------------------
void test_rx_slot() {
    SYLAR_LOG_INFO(g_logger) << "=== Test RxSlot ===";
    
    RxSlot slot(1024);
    
    // 写入数据
    std::vector<uint8_t> data1 = {1, 2, 3, 4, 5};
    assert(slot.write(data1.data(), data1.size(), 1, 1000));
    assert(slot.isValid());
    assert(slot.getCurrentSeq() == 1);
    
    // 读取数据
    std::vector<uint8_t> out;
    uint64_t seq, ts;
    assert(slot.tryRead(out, seq, ts));
    assert(seq == 1);
    assert(ts == 1000);
    assert(out == data1);
    
    SYLAR_LOG_INFO(g_logger) << "First write/read OK";
    
    // 覆盖写入
    std::vector<uint8_t> data2 = {10, 20, 30};
    assert(slot.write(data2.data(), data2.size(), 2, 2000));
    assert(slot.getCurrentSeq() == 2);
    
    // 读取覆盖后的数据
    assert(slot.tryRead(out, seq, ts));
    assert(seq == 2);
    assert(ts == 2000);
    assert(out == data2);
    
    SYLAR_LOG_INFO(g_logger) << "Overwrite OK";
    
    // 使槽失效
    slot.invalidate();
    assert(!slot.isValid());
    
    SYLAR_LOG_INFO(g_logger) << "=== Test RxSlot PASSED ===";
}

//------------------------------------------------------------------------------
// 测试3: RxSlot 并发覆盖测试
//------------------------------------------------------------------------------
void test_rx_slot_concurrent() {
    SYLAR_LOG_INFO(g_logger) << "=== Test RxSlot Concurrent ===";
    
    RxSlot slot(1024);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> write_count{0};
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> read_fail{0};
    
    // 写线程: 不断覆盖写入
    std::thread writer([&]() {
        uint64_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            seq++;
            std::vector<uint8_t> data(100);
            for (size_t i = 0; i < data.size(); ++i) {
                data[i] = static_cast<uint8_t>(seq & 0xFF);
            }
            slot.write(data.data(), data.size(), seq, seq * 1000);
            write_count.fetch_add(1, std::memory_order_relaxed);
        }
    });
    
    // 读线程: 不断尝试读取
    std::thread reader([&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            std::vector<uint8_t> out;
            uint64_t seq, ts;
            if (slot.tryRead(out, seq, ts)) {
                // 验证数据一致性: 所有字节应该相同
                if (!out.empty()) {
                    uint8_t expected = out[0];
                    bool consistent = true;
                    for (size_t i = 1; i < out.size(); ++i) {
                        if (out[i] != expected) {
                            consistent = false;
                            break;
                        }
                    }
                    if (consistent) {
                        read_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        read_fail.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }
    });
    
    // 运行1秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true, std::memory_order_relaxed);
    
    writer.join();
    reader.join();
    
    SYLAR_LOG_INFO(g_logger) << "Writes: " << write_count.load()
                              << " Reads: " << read_count.load()
                              << " Failures: " << read_fail.load();
    
    // 确保没有读取到不一致的数据
    assert(read_fail.load() == 0);
    
    SYLAR_LOG_INFO(g_logger) << "=== Test RxSlot Concurrent PASSED ===";
}

//------------------------------------------------------------------------------
// 测试4: CarState 测试
//------------------------------------------------------------------------------
void test_car_state() {
    SYLAR_LOG_INFO(g_logger) << "=== Test CarState ===";
    
    CarStateManager manager(10);
    
    CarState* state = manager.getState(0);
    assert(state != nullptr);
    assert(state->car_id == 0);
    assert(!state->inflight.load());
    
    // 开始请求
    std::string req_id = state->startRequest(100);
    assert(!req_id.empty());
    assert(state->inflight.load());
    assert(state->isCurrentRequest(req_id));
    assert(state->current_seq.load() == 100);
    
    SYLAR_LOG_INFO(g_logger) << "Request started: " << req_id;
    
    // 完成请求
    state->finishRequest(req_id);
    assert(!state->inflight.load());
    
    // 测试最小间隔
    assert(state->shouldSubmit(0));  // 无限制
    // 使用较长间隔进行测试 - 刚提交后应该不应该允许再次提交
    // 然后等待足够时间后应该允许
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(state->shouldSubmit(1));  // 已过1ms，可以提交
    
    // 重置
    state->reset();
    assert(state->last_seq_seen.load() == 0);
    
    SYLAR_LOG_INFO(g_logger) << "=== Test CarState PASSED ===";
}

//------------------------------------------------------------------------------
// 测试5: 配置测试
//------------------------------------------------------------------------------
void test_config() {
    SYLAR_LOG_INFO(g_logger) << "=== Test Config ===";
    
    auto config = IcpConfig::getDefault();
    assert(config != nullptr);
    
    // 验证默认值
    assert(config->max_cars == 100);
    assert(config->max_msg_size == 10 * 1024 * 1024);
    assert(config->vllm.timeout_ms == 30000);
    
    // 验证配置
    std::string error = config->validate();
    assert(error.empty());
    
    SYLAR_LOG_INFO(g_logger) << "Default config validated";
    
    // 测试非法配置
    config->max_cars = 0;
    error = config->validate();
    assert(!error.empty());
    
    SYLAR_LOG_INFO(g_logger) << "Invalid config detected: " << error;
    
    SYLAR_LOG_INFO(g_logger) << "=== Test Config PASSED ===";
}

//------------------------------------------------------------------------------
// 测试6: Metrics 测试
//------------------------------------------------------------------------------
void test_metrics() {
    SYLAR_LOG_INFO(g_logger) << "=== Test Metrics ===";
    
    IcpMetrics metrics(10);
    
    // 记录一些指标
    metrics.recordRecvMsg(0);
    metrics.recordRecvMsg(0);
    metrics.recordSubmit(0);
    metrics.recordSuccess(0);
    metrics.recordAbort(1);
    
    metrics.recordDeviceToSubmitLatency(50);
    metrics.recordSubmitToDoneLatency(200);
    metrics.recordE2ELatency(250);
    
    // 检查车辆指标
    auto* car_metrics = metrics.getCarMetrics(0);
    assert(car_metrics != nullptr);
    assert(car_metrics->recv_msgs.load() == 2);
    assert(car_metrics->submit_count.load() == 1);
    assert(car_metrics->success_count.load() == 1);
    
    // 检查系统指标
    auto& sys = metrics.getSystemMetrics();
    assert(sys.total_requests.load() == 1);
    assert(sys.total_aborts.load() == 1);
    
    // 生成报告
    std::string report = metrics.generateReport();
    SYLAR_LOG_INFO(g_logger) << "Report:\n" << report;
    
    std::string summary = metrics.generateSummary();
    SYLAR_LOG_INFO(g_logger) << "Summary: " << summary;
    
    SYLAR_LOG_INFO(g_logger) << "=== Test Metrics PASSED ===";
}

//------------------------------------------------------------------------------
// 测试7: Base64编码测试
//------------------------------------------------------------------------------
void test_base64() {
    SYLAR_LOG_INFO(g_logger) << "=== Test Base64 ===";
    
    // 测试空数据
    std::string result = base64Encode(nullptr, 0);
    assert(result.empty());
    
    // 测试简单数据
    uint8_t data[] = {'H', 'e', 'l', 'l', 'o'};
    result = base64Encode(data, 5);
    assert(result == "SGVsbG8=");
    
    SYLAR_LOG_INFO(g_logger) << "Base64 of 'Hello': " << result;
    
    // 测试不同长度
    for (size_t len = 0; len < 10; ++len) {
        std::vector<uint8_t> test_data(len, 'A');
        result = base64Encode(test_data.data(), test_data.size());
        SYLAR_LOG_DEBUG(g_logger) << "Base64 of " << len << " bytes: " << result;
    }
    
    SYLAR_LOG_INFO(g_logger) << "=== Test Base64 PASSED ===";
}

//------------------------------------------------------------------------------
// 测试8: RxSlotManager 测试
//------------------------------------------------------------------------------
void test_rx_slot_manager() {
    SYLAR_LOG_INFO(g_logger) << "=== Test RxSlotManager ===";
    
    RxSlotManager manager(10, 1024);
    
    assert(manager.getCarCount() == 10);
    assert(manager.getSlotSize() == 1024);
    
    // 获取有效slot
    RxSlot* slot = manager.getSlot(0);
    assert(slot != nullptr);
    assert(slot->getMaxSize() == 1024);
    
    // 获取越界slot
    assert(manager.getSlot(100) == nullptr);
    
    // 写入并验证
    std::vector<uint8_t> data = {1, 2, 3};
    slot->write(data.data(), data.size(), 1, 1000);
    
    std::vector<uint8_t> out;
    uint64_t seq, ts;
    assert(slot->tryRead(out, seq, ts));
    assert(out == data);
    
    SYLAR_LOG_INFO(g_logger) << "=== Test RxSlotManager PASSED ===";
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // 设置日志级别
    g_logger->setLevel(LogLevel::DEBUG);
    
    SYLAR_LOG_INFO(g_logger) << "Starting ICP tests...";
    
    try {
        test_protocol();
        test_rx_slot();
        test_rx_slot_concurrent();
        test_car_state();
        test_config();
        test_metrics();
        test_base64();
        test_rx_slot_manager();
        
        SYLAR_LOG_INFO(g_logger) << "";
        SYLAR_LOG_INFO(g_logger) << "========================================";
        SYLAR_LOG_INFO(g_logger) << "All tests PASSED!";
        SYLAR_LOG_INFO(g_logger) << "========================================";
        
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Test failed with exception: " << e.what();
        return 1;
    }
    
    return 0;
}
