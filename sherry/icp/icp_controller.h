#ifndef __SHERRY_ICP_CONTROLLER_H__
#define __SHERRY_ICP_CONTROLLER_H__

#include <memory>
#include <atomic>
#include <functional>
#include <queue>

#include "icp_config.h"
#include "icp_protocol.h"
#include "icp_rx_slot.h"
#include "icp_car_state.h"
#include "icp_vllm_client.h"
#include "icp_metrics.h"
#include "sherry/thread.h"
#include "sherry/iomanager.h"
#include "sherry/socket.h"

namespace sherry {
namespace icp {

/**
 * @brief 结果发送回调
 */
typedef std::function<void(uint32_t car_id, const OutputMessage& msg)> ResultCallback;

/**
 * @brief ICP 控制平面
 * 
 * 核心职责:
 * - per-car 状态机维护: last_seq_seen, current_req_id, inflight
 * - Latest-Only 逻辑:
 *   - 若新 seq 到来且 inflight: 立即 abort(current_req_id)
 *   - 提交新推理请求
 * - Admission/Drop:
 *   - 可以按时间间隔做采样
 *   - 若 vLLM 端拥塞, 可直接 drop 旧观测
 * - 维护映射: request_id -> car_id + seq
 */
class IcpController : public VllmCallback {
public:
    typedef std::shared_ptr<IcpController> ptr;
    
    /**
     * @brief 构造函数
     * @param config ICP配置
     */
    explicit IcpController(IcpConfig::ptr config);
    
    ~IcpController();
    
    /**
     * @brief 初始化控制器
     * @param io_manager 控制线程的IOManager
     * @param http_io_manager HTTP请求的IOManager
     * @return true表示成功
     */
    bool init(IOManager* io_manager, IOManager* http_io_manager);
    
    /**
     * @brief 通知有新消息到达(由IO线程调用)
     * @param car_id 车辆ID
     * @param seq 序列号
     * 
     * 这个方法是线程安全的,会将通知异步调度到控制线程
     */
    void notifyNewMessage(uint32_t car_id, uint64_t seq);
    
    /**
     * @brief 设置结果回调
     */
    void setResultCallback(ResultCallback callback);
    
    /**
     * @brief 获取RxSlot管理器
     */
    RxSlotManager* getRxSlotManager() { return m_rxSlots.get(); }
    
    /**
     * @brief 获取指标收集器
     */
    IcpMetrics::ptr getMetrics() { return m_metrics; }
    
    /**
     * @brief 获取配置
     */
    IcpConfig::ptr getConfig() { return m_config; }
    
    /**
     * @brief 停止控制器
     */
    void stop();
    
    // VllmCallback 接口实现
    void onComplete(const VllmResult& result) override;
    void onFirstToken(const std::string& request_id, uint64_t time_ms) override;
    
private:
    /**
     * @brief 处理车辆消息(在控制线程执行)
     */
    void processCarMessage(uint32_t car_id);
    
    /**
     * @brief 提交推理请求
     */
    void submitInference(uint32_t car_id, uint64_t seq,
                         const std::vector<uint8_t>& data);
    
    /**
     * @brief 处理推理结果
     */
    void handleResult(const VllmResult& result);
    
    /**
     * @brief 解析waypoint输出
     */
    std::string parseWaypoints(const std::string& output_text);
    
private:
    IcpConfig::ptr m_config;
    IcpMetrics::ptr m_metrics;
    
    // 核心组件
    std::unique_ptr<RxSlotManager> m_rxSlots;
    std::unique_ptr<CarStateManager> m_carStates;
    std::unique_ptr<VllmClient> m_vllmClient;
    
    // IOManager
    IOManager* m_controlIoManager;
    IOManager* m_httpIoManager;
    
    // 结果回调
    ResultCallback m_resultCallback;
    mutable Mutex m_callbackMutex;
    
    std::atomic<bool> m_running{false};
};

/**
 * @brief 通知队列(用于IO线程到控制线程的通信)
 * 
 * 简化版: 使用条件变量
 * 生产环境可以使用eventfd或lock-free queue
 */
class NotifyQueue {
public:
    typedef std::shared_ptr<NotifyQueue> ptr;
    
    struct Notification {
        uint32_t car_id;
        uint64_t seq;
    };
    
    /**
     * @brief 推入通知
     */
    void push(uint32_t car_id, uint64_t seq);
    
    /**
     * @brief 弹出通知(阻塞)
     */
    bool pop(Notification& out, uint32_t timeout_ms = 0);
    
    /**
     * @brief 尝试弹出所有通知
     */
    std::vector<Notification> popAll();
    
    /**
     * @brief 唤醒等待的线程
     */
    void wakeup();
    
private:
    std::queue<Notification> m_queue;
    Mutex m_mutex;
    Semaphore m_sem;
};

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_CONTROLLER_H__
