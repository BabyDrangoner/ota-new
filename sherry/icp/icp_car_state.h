#ifndef __SHERRY_ICP_CAR_STATE_H__
#define __SHERRY_ICP_CAR_STATE_H__

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>

#include "sherry/thread.h"

namespace sherry {
namespace icp {

/**
 * @brief 单个车辆的状态
 * 
 * 用于 Latest-Only 控制逻辑:
 * - 跟踪已见的最大序列号
 * - 跟踪当前 inflight 的推理请求
 * - 支持 abort 旧请求
 */
struct CarState {
    typedef std::shared_ptr<CarState> ptr;
    
    uint32_t car_id;                      // 车辆ID
    
    // 序列号管理
    std::atomic<uint64_t> last_seq_seen{0};   // 最后处理的序列号
    std::atomic<uint64_t> current_seq{0};     // 当前正在处理的序列号
    
    // 推理状态
    std::atomic<bool> inflight{false};        // 是否有 inflight 请求
    std::string current_request_id;           // 当前请求ID (需要加锁保护)
    
    // 时间统计
    std::atomic<uint64_t> last_submit_time_ms{0};   // 最后提交时间
    std::atomic<uint64_t> device_timestamp_ms{0};   // 设备端时间戳
    
    // 锁保护 request_id
    mutable Mutex mutex;
    
    CarState(uint32_t id) : car_id(id) {}
    
    /**
     * @brief 更新当前请求ID
     */
    void setRequestId(const std::string& req_id) {
        Mutex::Lock lock(mutex);
        current_request_id = req_id;
    }
    
    /**
     * @brief 获取当前请求ID
     */
    std::string getRequestId() const {
        Mutex::Lock lock(mutex);
        return current_request_id;
    }
    
    /**
     * @brief 检查给定的请求ID是否仍然是当前请求
     */
    bool isCurrentRequest(const std::string& req_id) const {
        Mutex::Lock lock(mutex);
        return current_request_id == req_id;
    }
    
    /**
     * @brief 开始新的推理请求
     * @return 新的请求ID
     */
    std::string startRequest(uint64_t seq);
    
    /**
     * @brief 完成推理请求
     */
    void finishRequest(const std::string& req_id);
    
    /**
     * @brief 检查是否应该提交新请求(基于最小间隔)
     * @param min_interval_ms 最小提交间隔(毫秒)
     */
    bool shouldSubmit(uint64_t min_interval_ms) const;
    
    /**
     * @brief 重置状态
     */
    void reset();
};

/**
 * @brief 请求 -> 车辆映射信息
 */
struct RequestInfo {
    uint32_t car_id;
    uint64_t seq;
    uint64_t submit_time_ms;
    uint64_t device_timestamp_ms;
};

/**
 * @brief 车辆状态管理器
 */
class CarStateManager {
public:
    typedef std::shared_ptr<CarStateManager> ptr;
    typedef std::unordered_map<std::string, RequestInfo> RequestMap;
    
    /**
     * @brief 构造函数
     * @param max_cars 最大车辆数
     */
    explicit CarStateManager(size_t max_cars);
    
    /**
     * @brief 获取车辆状态
     * @param car_id 车辆ID
     * @return 车辆状态指针, 如果car_id超出范围返回nullptr
     */
    CarState* getState(uint32_t car_id);
    
    /**
     * @brief 注册请求
     * @param request_id 请求ID
     * @param info 请求信息
     */
    void registerRequest(const std::string& request_id, const RequestInfo& info);
    
    /**
     * @brief 取消注册请求
     * @param request_id 请求ID
     */
    void unregisterRequest(const std::string& request_id);
    
    /**
     * @brief 查找请求信息
     * @param request_id 请求ID
     * @param out_info 输出请求信息
     * @return true 如果找到
     */
    bool findRequest(const std::string& request_id, RequestInfo& out_info) const;
    
    /**
     * @brief 获取 inflight 请求数
     */
    size_t getInflightCount() const;
    
    /**
     * @brief 获取最大车辆数
     */
    size_t getMaxCars() const { return m_maxCars; }
    
private:
    size_t m_maxCars;
    std::vector<CarState::ptr> m_states;
    
    // 请求映射(需要锁保护)
    mutable RWMutex m_requestMutex;
    RequestMap m_requests;
};

/**
 * @brief 生成唯一请求ID
 */
std::string generateRequestId();

/**
 * @brief 获取当前时间戳(毫秒)
 */
inline uint64_t getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

/**
 * @brief 获取系统时间戳(毫秒)
 */
inline uint64_t getSystemTimeMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_CAR_STATE_H__
