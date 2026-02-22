#include "icp_car_state.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace sherry {
namespace icp {

//------------------------------------------------------------------------------
// 辅助函数
//------------------------------------------------------------------------------

std::string generateRequestId() {
    // 使用随机数生成UUID风格的请求ID
    static thread_local std::mt19937 gen(
        std::random_device{}() ^ 
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())
    );
    
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << part1
        << std::setw(16) << part2;
    return oss.str();
}

//------------------------------------------------------------------------------
// CarState
//------------------------------------------------------------------------------

std::string CarState::startRequest(uint64_t seq) {
    std::string req_id = generateRequestId();
    
    {
        Mutex::Lock lock(mutex);
        current_request_id = req_id;
    }
    
    current_seq.store(seq, std::memory_order_release);
    inflight.store(true, std::memory_order_release);
    last_submit_time_ms.store(getCurrentTimeMs(), std::memory_order_release);
    
    return req_id;
}

void CarState::finishRequest(const std::string& req_id) {
    Mutex::Lock lock(mutex);
    
    // 只有当请求ID匹配时才清除状态
    if (current_request_id == req_id) {
        inflight.store(false, std::memory_order_release);
        // 不清除 current_request_id, 用于后续过期结果判断
    }
}

bool CarState::shouldSubmit(uint64_t min_interval_ms) const {
    if (min_interval_ms == 0) {
        return true;
    }
    
    uint64_t last_time = last_submit_time_ms.load(std::memory_order_acquire);
    uint64_t now = getCurrentTimeMs();
    
    return (now - last_time) >= min_interval_ms;
}

void CarState::reset() {
    Mutex::Lock lock(mutex);
    
    last_seq_seen.store(0, std::memory_order_release);
    current_seq.store(0, std::memory_order_release);
    inflight.store(false, std::memory_order_release);
    current_request_id.clear();
    last_submit_time_ms.store(0, std::memory_order_release);
    device_timestamp_ms.store(0, std::memory_order_release);
}

//------------------------------------------------------------------------------
// CarStateManager
//------------------------------------------------------------------------------

CarStateManager::CarStateManager(size_t max_cars)
    : m_maxCars(max_cars) {
    m_states.reserve(max_cars);
    for (size_t i = 0; i < max_cars; ++i) {
        m_states.push_back(std::make_shared<CarState>(static_cast<uint32_t>(i)));
    }
}

CarState* CarStateManager::getState(uint32_t car_id) {
    if (car_id >= m_maxCars) {
        return nullptr;
    }
    return m_states[car_id].get();
}

void CarStateManager::registerRequest(const std::string& request_id, 
                                       const RequestInfo& info) {
    RWMutex::WriteLock lock(m_requestMutex);
    m_requests[request_id] = info;
}

void CarStateManager::unregisterRequest(const std::string& request_id) {
    RWMutex::WriteLock lock(m_requestMutex);
    m_requests.erase(request_id);
}

bool CarStateManager::findRequest(const std::string& request_id, 
                                   RequestInfo& out_info) const {
    RWMutex::ReadLock lock(m_requestMutex);
    auto it = m_requests.find(request_id);
    if (it == m_requests.end()) {
        return false;
    }
    out_info = it->second;
    return true;
}

size_t CarStateManager::getInflightCount() const {
    size_t count = 0;
    for (const auto& state : m_states) {
        if (state->inflight.load(std::memory_order_relaxed)) {
            ++count;
        }
    }
    return count;
}

} // namespace icp
} // namespace sherry
