#include "icp_metrics.h"
#include "sherry/log.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace sherry {
namespace icp {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("icp");

//------------------------------------------------------------------------------
// LatencyHistogram
//------------------------------------------------------------------------------

constexpr uint64_t LatencyHistogram::BUCKET_BOUNDS[BUCKET_COUNT];

LatencyHistogram::LatencyHistogram() {
    reset();
}

void LatencyHistogram::record(uint64_t latency_ms) {
    // 找到对应的bucket
    size_t bucket = BUCKET_COUNT;  // 默认overflow bucket
    for (size_t i = 0; i < BUCKET_COUNT; ++i) {
        if (latency_ms <= BUCKET_BOUNDS[i]) {
            bucket = i;
            break;
        }
    }
    
    m_buckets[bucket].fetch_add(1, std::memory_order_relaxed);
    m_count.fetch_add(1, std::memory_order_relaxed);
    m_sum.fetch_add(latency_ms, std::memory_order_relaxed);
}

void LatencyHistogram::reset() {
    for (size_t i = 0; i <= BUCKET_COUNT; ++i) {
        m_buckets[i].store(0, std::memory_order_relaxed);
    }
    m_count.store(0, std::memory_order_relaxed);
    m_sum.store(0, std::memory_order_relaxed);
}

std::vector<uint64_t> LatencyHistogram::getBuckets() const {
    std::vector<uint64_t> result(BUCKET_COUNT + 1);
    for (size_t i = 0; i <= BUCKET_COUNT; ++i) {
        result[i] = m_buckets[i].load(std::memory_order_relaxed);
    }
    return result;
}

uint64_t LatencyHistogram::getCount() const {
    return m_count.load(std::memory_order_relaxed);
}

uint64_t LatencyHistogram::getSum() const {
    return m_sum.load(std::memory_order_relaxed);
}

double LatencyHistogram::getAverage() const {
    uint64_t count = m_count.load(std::memory_order_relaxed);
    if (count == 0) return 0.0;
    return static_cast<double>(m_sum.load(std::memory_order_relaxed)) / count;
}

uint64_t LatencyHistogram::getPercentile(double p) const {
    uint64_t total = m_count.load(std::memory_order_relaxed);
    if (total == 0) return 0;
    
    uint64_t target = static_cast<uint64_t>(total * p);
    uint64_t cumulative = 0;
    
    for (size_t i = 0; i <= BUCKET_COUNT; ++i) {
        cumulative += m_buckets[i].load(std::memory_order_relaxed);
        if (cumulative >= target) {
            if (i < BUCKET_COUNT) {
                return BUCKET_BOUNDS[i];
            }
            return BUCKET_BOUNDS[BUCKET_COUNT - 1] * 2;  // overflow估计
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
// CarMetrics
//------------------------------------------------------------------------------

void CarMetrics::reset() {
    recv_msgs.store(0, std::memory_order_relaxed);
    drop_stale.store(0, std::memory_order_relaxed);
    drop_overflow.store(0, std::memory_order_relaxed);
    abort_count.store(0, std::memory_order_relaxed);
    submit_count.store(0, std::memory_order_relaxed);
    success_count.store(0, std::memory_order_relaxed);
    error_count.store(0, std::memory_order_relaxed);
    timeout_count.store(0, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
// SystemMetrics
//------------------------------------------------------------------------------

void SystemMetrics::reset() {
    io_read_bytes.store(0, std::memory_order_relaxed);
    io_write_bytes.store(0, std::memory_order_relaxed);
    io_read_count.store(0, std::memory_order_relaxed);
    io_write_count.store(0, std::memory_order_relaxed);
    io_error_count.store(0, std::memory_order_relaxed);
    active_connections.store(0, std::memory_order_relaxed);
    total_connections.store(0, std::memory_order_relaxed);
    inflight_requests.store(0, std::memory_order_relaxed);
    total_requests.store(0, std::memory_order_relaxed);
    total_aborts.store(0, std::memory_order_relaxed);
    http_requests.store(0, std::memory_order_relaxed);
    http_errors.store(0, std::memory_order_relaxed);
    http_timeouts.store(0, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
// LatencyMetrics
//------------------------------------------------------------------------------

void LatencyMetrics::reset() {
    device_to_submit.reset();
    submit_to_first_token.reset();
    submit_to_done.reset();
    total_e2e.reset();
}

//------------------------------------------------------------------------------
// IcpMetrics
//------------------------------------------------------------------------------

IcpMetrics::IcpMetrics(size_t max_cars)
    : m_maxCars(max_cars)
    , m_carMetrics(max_cars) {
}

CarMetrics* IcpMetrics::getCarMetrics(uint32_t car_id) {
    if (car_id >= m_maxCars) {
        return nullptr;
    }
    return &m_carMetrics[car_id];
}

void IcpMetrics::reset() {
    for (auto& cm : m_carMetrics) {
        cm.reset();
    }
    m_system.reset();
    m_latency.reset();
}

std::string IcpMetrics::generateReport() const {
    std::ostringstream oss;
    oss << "{\n";
    
    // 系统指标
    oss << "  \"system\": {\n";
    oss << "    \"io_read_bytes\": " << m_system.io_read_bytes.load() << ",\n";
    oss << "    \"io_write_bytes\": " << m_system.io_write_bytes.load() << ",\n";
    oss << "    \"io_read_count\": " << m_system.io_read_count.load() << ",\n";
    oss << "    \"io_write_count\": " << m_system.io_write_count.load() << ",\n";
    oss << "    \"io_error_count\": " << m_system.io_error_count.load() << ",\n";
    oss << "    \"active_connections\": " << m_system.active_connections.load() << ",\n";
    oss << "    \"total_connections\": " << m_system.total_connections.load() << ",\n";
    oss << "    \"inflight_requests\": " << m_system.inflight_requests.load() << ",\n";
    oss << "    \"total_requests\": " << m_system.total_requests.load() << ",\n";
    oss << "    \"total_aborts\": " << m_system.total_aborts.load() << ",\n";
    oss << "    \"http_requests\": " << m_system.http_requests.load() << ",\n";
    oss << "    \"http_errors\": " << m_system.http_errors.load() << ",\n";
    oss << "    \"http_timeouts\": " << m_system.http_timeouts.load() << "\n";
    oss << "  },\n";
    
    // 延迟指标
    oss << "  \"latency\": {\n";
    oss << "    \"device_to_submit_avg_ms\": " 
        << std::fixed << std::setprecision(2) 
        << m_latency.device_to_submit.getAverage() << ",\n";
    oss << "    \"submit_to_first_token_avg_ms\": " 
        << m_latency.submit_to_first_token.getAverage() << ",\n";
    oss << "    \"submit_to_done_avg_ms\": " 
        << m_latency.submit_to_done.getAverage() << ",\n";
    oss << "    \"total_e2e_avg_ms\": " 
        << m_latency.total_e2e.getAverage() << ",\n";
    oss << "    \"total_e2e_p99_ms\": " 
        << m_latency.total_e2e.getPercentile(0.99) << "\n";
    oss << "  }\n";
    
    oss << "}\n";
    return oss.str();
}

std::string IcpMetrics::generateSummary() const {
    std::ostringstream oss;
    oss << "ICP metrics: "
        << "conn=" << m_system.active_connections.load()
        << " inflight=" << m_system.inflight_requests.load()
        << " req=" << m_system.total_requests.load()
        << " abort=" << m_system.total_aborts.load()
        << " e2e_avg=" << std::fixed << std::setprecision(1) 
        << m_latency.total_e2e.getAverage() << "ms"
        << " p99=" << m_latency.total_e2e.getPercentile(0.99) << "ms";
    return oss.str();
}

void IcpMetrics::recordRecvMsg(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->recv_msgs.fetch_add(1, std::memory_order_relaxed);
    }
}

void IcpMetrics::recordDropStale(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->drop_stale.fetch_add(1, std::memory_order_relaxed);
    }
}

void IcpMetrics::recordAbort(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->abort_count.fetch_add(1, std::memory_order_relaxed);
    }
    m_system.total_aborts.fetch_add(1, std::memory_order_relaxed);
}

void IcpMetrics::recordSubmit(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->submit_count.fetch_add(1, std::memory_order_relaxed);
    }
    m_system.total_requests.fetch_add(1, std::memory_order_relaxed);
    m_system.inflight_requests.fetch_add(1, std::memory_order_relaxed);
}

void IcpMetrics::recordSuccess(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->success_count.fetch_add(1, std::memory_order_relaxed);
    }
    m_system.inflight_requests.fetch_sub(1, std::memory_order_relaxed);
}

void IcpMetrics::recordError(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->error_count.fetch_add(1, std::memory_order_relaxed);
    }
    m_system.inflight_requests.fetch_sub(1, std::memory_order_relaxed);
}

void IcpMetrics::recordTimeout(uint32_t car_id) {
    if (auto* cm = getCarMetrics(car_id)) {
        cm->timeout_count.fetch_add(1, std::memory_order_relaxed);
    }
    m_system.http_timeouts.fetch_add(1, std::memory_order_relaxed);
    m_system.inflight_requests.fetch_sub(1, std::memory_order_relaxed);
}

void IcpMetrics::recordIORead(size_t bytes) {
    m_system.io_read_bytes.fetch_add(bytes, std::memory_order_relaxed);
    m_system.io_read_count.fetch_add(1, std::memory_order_relaxed);
}

void IcpMetrics::recordIOWrite(size_t bytes) {
    m_system.io_write_bytes.fetch_add(bytes, std::memory_order_relaxed);
    m_system.io_write_count.fetch_add(1, std::memory_order_relaxed);
}

void IcpMetrics::recordIOError() {
    m_system.io_error_count.fetch_add(1, std::memory_order_relaxed);
}

void IcpMetrics::recordConnectionOpen() {
    m_system.active_connections.fetch_add(1, std::memory_order_relaxed);
    m_system.total_connections.fetch_add(1, std::memory_order_relaxed);
}

void IcpMetrics::recordConnectionClose() {
    m_system.active_connections.fetch_sub(1, std::memory_order_relaxed);
}

void IcpMetrics::recordDeviceToSubmitLatency(uint64_t ms) {
    m_latency.device_to_submit.record(ms);
}

void IcpMetrics::recordSubmitToFirstTokenLatency(uint64_t ms) {
    m_latency.submit_to_first_token.record(ms);
}

void IcpMetrics::recordSubmitToDoneLatency(uint64_t ms) {
    m_latency.submit_to_done.record(ms);
}

void IcpMetrics::recordE2ELatency(uint64_t ms) {
    m_latency.total_e2e.record(ms);
}

//------------------------------------------------------------------------------
// MetricsReporter
//------------------------------------------------------------------------------

MetricsReporter::MetricsReporter(IcpMetrics::ptr metrics, uint32_t interval_ms)
    : m_metrics(metrics)
    , m_intervalMs(interval_ms) {
}

void MetricsReporter::start() {
    m_running.store(true, std::memory_order_release);
    // TODO: 启动定时器线程
    SYLAR_LOG_INFO(g_logger) << "MetricsReporter started with interval " 
                              << m_intervalMs << "ms";
}

void MetricsReporter::stop() {
    m_running.store(false, std::memory_order_release);
    SYLAR_LOG_INFO(g_logger) << "MetricsReporter stopped";
}

} // namespace icp
} // namespace sherry
