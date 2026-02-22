#ifndef __SHERRY_ICP_METRICS_H__
#define __SHERRY_ICP_METRICS_H__

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

#include "sherry/thread.h"

namespace sherry {
namespace icp {

/**
 * @brief 延迟直方图(简化版)
 */
class LatencyHistogram {
public:
    // 预定义的bucket边界(毫秒)
    static constexpr size_t BUCKET_COUNT = 10;
    static constexpr uint64_t BUCKET_BOUNDS[BUCKET_COUNT] = {
        10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000
    };
    
    LatencyHistogram();
    
    void record(uint64_t latency_ms);
    
    void reset();
    
    // 获取各bucket的计数
    std::vector<uint64_t> getBuckets() const;
    
    // 获取总计数
    uint64_t getCount() const;
    
    // 获取总和
    uint64_t getSum() const;
    
    // 获取平均值
    double getAverage() const;
    
    // 获取指定百分位数(近似值)
    uint64_t getPercentile(double p) const;
    
private:
    std::atomic<uint64_t> m_buckets[BUCKET_COUNT + 1];  // +1 for overflow
    std::atomic<uint64_t> m_count{0};
    std::atomic<uint64_t> m_sum{0};
};

/**
 * @brief 单车指标
 */
struct CarMetrics {
    std::atomic<uint64_t> recv_msgs{0};          // 接收消息数
    std::atomic<uint64_t> drop_stale{0};         // 丢弃的过期消息数
    std::atomic<uint64_t> drop_overflow{0};      // 丢弃的溢出消息数
    std::atomic<uint64_t> abort_count{0};        // abort次数
    std::atomic<uint64_t> submit_count{0};       // 提交次数
    std::atomic<uint64_t> success_count{0};      // 成功次数
    std::atomic<uint64_t> error_count{0};        // 错误次数
    std::atomic<uint64_t> timeout_count{0};      // 超时次数
    
    void reset();
};

/**
 * @brief 系统级指标
 */
struct SystemMetrics {
    // IO 相关
    std::atomic<uint64_t> io_read_bytes{0};      // 读取字节数
    std::atomic<uint64_t> io_write_bytes{0};     // 写入字节数
    std::atomic<uint64_t> io_read_count{0};      // 读取次数
    std::atomic<uint64_t> io_write_count{0};     // 写入次数
    std::atomic<uint64_t> io_error_count{0};     // IO错误次数
    
    // 连接相关
    std::atomic<uint64_t> active_connections{0}; // 活跃连接数
    std::atomic<uint64_t> total_connections{0};  // 累计连接数
    
    // 推理相关
    std::atomic<uint64_t> inflight_requests{0};  // 正在处理的请求数
    std::atomic<uint64_t> total_requests{0};     // 累计请求数
    std::atomic<uint64_t> total_aborts{0};       // 累计abort数
    
    // HTTP 相关
    std::atomic<uint64_t> http_requests{0};      // HTTP请求数
    std::atomic<uint64_t> http_errors{0};        // HTTP错误数
    std::atomic<uint64_t> http_timeouts{0};      // HTTP超时数
    
    void reset();
};

/**
 * @brief 延迟指标
 */
struct LatencyMetrics {
    LatencyHistogram device_to_submit;           // 设备到提交延迟
    LatencyHistogram submit_to_first_token;      // 提交到首token延迟
    LatencyHistogram submit_to_done;             // 提交到完成延迟
    LatencyHistogram total_e2e;                  // 端到端延迟
    
    void reset();
};

/**
 * @brief ICP 指标管理器
 */
class IcpMetrics {
public:
    typedef std::shared_ptr<IcpMetrics> ptr;
    
    /**
     * @brief 构造函数
     * @param max_cars 最大车辆数
     */
    explicit IcpMetrics(size_t max_cars);
    
    /**
     * @brief 获取单车指标
     */
    CarMetrics* getCarMetrics(uint32_t car_id);
    
    /**
     * @brief 获取系统指标
     */
    SystemMetrics& getSystemMetrics() { return m_system; }
    
    /**
     * @brief 获取延迟指标
     */
    LatencyMetrics& getLatencyMetrics() { return m_latency; }
    
    /**
     * @brief 重置所有指标
     */
    void reset();
    
    /**
     * @brief 生成报告(JSON格式)
     */
    std::string generateReport() const;
    
    /**
     * @brief 生成摘要报告(一行)
     */
    std::string generateSummary() const;
    
    // 便捷方法
    void recordRecvMsg(uint32_t car_id);
    void recordDropStale(uint32_t car_id);
    void recordAbort(uint32_t car_id);
    void recordSubmit(uint32_t car_id);
    void recordSuccess(uint32_t car_id);
    void recordError(uint32_t car_id);
    void recordTimeout(uint32_t car_id);
    
    void recordIORead(size_t bytes);
    void recordIOWrite(size_t bytes);
    void recordIOError();
    
    void recordConnectionOpen();
    void recordConnectionClose();
    
    void recordDeviceToSubmitLatency(uint64_t ms);
    void recordSubmitToFirstTokenLatency(uint64_t ms);
    void recordSubmitToDoneLatency(uint64_t ms);
    void recordE2ELatency(uint64_t ms);
    
private:
    size_t m_maxCars;
    std::vector<CarMetrics> m_carMetrics;
    SystemMetrics m_system;
    LatencyMetrics m_latency;
    
    mutable Mutex m_mutex;
};

/**
 * @brief 指标输出器(定期输出到日志)
 */
class MetricsReporter {
public:
    typedef std::shared_ptr<MetricsReporter> ptr;
    
    MetricsReporter(IcpMetrics::ptr metrics, uint32_t interval_ms);
    
    void start();
    void stop();
    
private:
    IcpMetrics::ptr m_metrics;
    uint32_t m_intervalMs;
    std::atomic<bool> m_running{false};
};

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_METRICS_H__
