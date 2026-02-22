#include "icp_controller.h"
#include "sherry/log.h"

#include <algorithm>
#include <sstream>

namespace sherry {
namespace icp {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("icp");

//------------------------------------------------------------------------------
// NotifyQueue
//------------------------------------------------------------------------------

void NotifyQueue::push(uint32_t car_id, uint64_t seq) {
    {
        Mutex::Lock lock(m_mutex);
        m_queue.push({car_id, seq});
    }
    m_sem.notify();
}

bool NotifyQueue::pop(Notification& out, uint32_t timeout_ms) {
    m_sem.wait();
    
    Mutex::Lock lock(m_mutex);
    if (m_queue.empty()) {
        return false;
    }
    
    out = m_queue.front();
    m_queue.pop();
    return true;
}

std::vector<NotifyQueue::Notification> NotifyQueue::popAll() {
    std::vector<Notification> result;
    
    Mutex::Lock lock(m_mutex);
    while (!m_queue.empty()) {
        result.push_back(m_queue.front());
        m_queue.pop();
    }
    
    return result;
}

void NotifyQueue::wakeup() {
    m_sem.notify();
}

//------------------------------------------------------------------------------
// IcpController
//------------------------------------------------------------------------------

IcpController::IcpController(IcpConfig::ptr config)
    : m_config(config)
    , m_controlIoManager(nullptr)
    , m_httpIoManager(nullptr) {
    
    // 创建指标收集器
    m_metrics = std::make_shared<IcpMetrics>(config->max_cars);
    
    // 创建RxSlot管理器
    m_rxSlots = std::make_unique<RxSlotManager>(
        config->max_cars, config->max_msg_size);
    
    // 创建车辆状态管理器
    m_carStates = std::make_unique<CarStateManager>(config->max_cars);
}

IcpController::~IcpController() {
    stop();
}

bool IcpController::init(IOManager* io_manager, IOManager* http_io_manager) {
    if (!io_manager || !http_io_manager) {
        SYLAR_LOG_ERROR(g_logger) << "IcpController::init - IOManager is null";
        return false;
    }
    
    m_controlIoManager = io_manager;
    m_httpIoManager = http_io_manager;
    
    // 创建vLLM客户端
    m_vllmClient = std::make_unique<VllmClient>(
        m_config->vllm, m_metrics, this);
    
    if (!m_vllmClient->init(http_io_manager)) {
        SYLAR_LOG_ERROR(g_logger) << "IcpController::init - VllmClient init failed";
        return false;
    }
    
    m_running.store(true, std::memory_order_release);
    
    SYLAR_LOG_INFO(g_logger) << "IcpController initialized with "
                              << m_config->max_cars << " max cars";
    return true;
}

void IcpController::notifyNewMessage(uint32_t car_id, uint64_t seq) {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    
    // 异步调度到控制线程处理
    m_controlIoManager->schedule([this, car_id]() {
        processCarMessage(car_id);
    });
}

void IcpController::setResultCallback(ResultCallback callback) {
    Mutex::Lock lock(m_callbackMutex);
    m_resultCallback = std::move(callback);
}

void IcpController::stop() {
    m_running.store(false, std::memory_order_release);
    
    if (m_vllmClient) {
        m_vllmClient->stop();
    }
    
    SYLAR_LOG_INFO(g_logger) << "IcpController stopped";
}

void IcpController::processCarMessage(uint32_t car_id) {
    // 获取车辆状态
    CarState* state = m_carStates->getState(car_id);
    if (!state) {
        SYLAR_LOG_WARN(g_logger) << "Invalid car_id: " << car_id;
        return;
    }
    
    // 获取RxSlot
    RxSlot* slot = m_rxSlots->getSlot(car_id);
    if (!slot || !slot->isValid()) {
        return;
    }
    
    // 读取最新数据
    std::vector<uint8_t> data;
    uint64_t seq, ts;
    if (!slot->tryRead(data, seq, ts)) {
        return;  // 读取失败(可能被覆盖)
    }
    
    // Latest-Only: 检查是否是新消息
    uint64_t last_seen = state->last_seq_seen.load(std::memory_order_acquire);
    if (seq <= last_seen) {
        // 旧消息, 丢弃
        m_metrics->recordDropStale(car_id);
        return;
    }
    
    // 检查是否满足最小提交间隔
    if (!state->shouldSubmit(m_config->min_submit_interval_ms)) {
        // 未达到最小间隔, 跳过(避免abort风暴)
        return;
    }
    
    // 更新last_seq_seen
    state->last_seq_seen.store(seq, std::memory_order_release);
    
    // 如果有inflight请求, 先abort
    if (state->inflight.load(std::memory_order_acquire)) {
        std::string old_req_id = state->getRequestId();
        if (!old_req_id.empty()) {
            SYLAR_LOG_DEBUG(g_logger) << "Aborting old request: " << old_req_id
                                       << " car_id=" << car_id;
            m_vllmClient->abort(old_req_id);
            m_metrics->recordAbort(car_id);
        }
    }
    
    // 提交新的推理请求
    submitInference(car_id, seq, data);
}

void IcpController::submitInference(uint32_t car_id, uint64_t seq,
                                     const std::vector<uint8_t>& data) {
    CarState* state = m_carStates->getState(car_id);
    if (!state) {
        return;
    }
    
    // 解析消息
    ParsedMessage msg;
    auto result = MessageParser::parseMessage(data.data(), data.size(), msg);
    if (result != MessageParser::ParseResult::OK) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to parse message, car_id=" << car_id
                                   << " seq=" << seq
                                   << " error=" << static_cast<int>(result);
        return;
    }
    
    // 创建推理请求
    auto request = std::make_shared<VllmRequest>();
    request->request_id = state->startRequest(seq);
    request->car_id = car_id;
    request->seq = seq;
    request->device_timestamp_ms = msg.timestamp_ms;
    request->submit_time_ms = getCurrentTimeMs();
    request->prompt = msg.prompt.empty() ? 
        "Please analyze the images and provide navigation waypoints." : msg.prompt;
    
    // 编码图片为base64(仅在确定要提交时才编码)
    if (m_config->encode_images_base64) {
        request->images_base64 = encodeImagesToBase64(msg.images);
    }
    
    // 注册请求映射
    RequestInfo info;
    info.car_id = car_id;
    info.seq = seq;
    info.submit_time_ms = request->submit_time_ms;
    info.device_timestamp_ms = msg.timestamp_ms;
    m_carStates->registerRequest(request->request_id, info);
    
    // 记录延迟(设备到提交)
    if (m_metrics) {
        uint64_t device_to_submit = request->submit_time_ms - msg.timestamp_ms;
        m_metrics->recordDeviceToSubmitLatency(device_to_submit);
        m_metrics->recordSubmit(car_id);
    }
    
    SYLAR_LOG_DEBUG(g_logger) << "Submitting inference: request_id=" 
                               << request->request_id
                               << " car_id=" << car_id
                               << " seq=" << seq
                               << " images=" << msg.images.size();
    
    // 提交请求
    m_vllmClient->submit(request);
}

void IcpController::onComplete(const VllmResult& result) {
    // 异步调度到控制线程处理结果
    m_controlIoManager->schedule([this, result]() {
        handleResult(result);
    });
}

void IcpController::onFirstToken(const std::string& request_id, uint64_t time_ms) {
    // 记录首token延迟
    RequestInfo info;
    if (m_carStates->findRequest(request_id, info)) {
        uint64_t latency = time_ms - info.submit_time_ms;
        m_metrics->recordSubmitToFirstTokenLatency(latency);
    }
}

void IcpController::handleResult(const VllmResult& result) {
    // 查找请求信息
    RequestInfo info;
    if (!m_carStates->findRequest(result.request_id, info)) {
        SYLAR_LOG_WARN(g_logger) << "Unknown request: " << result.request_id;
        return;
    }
    
    // 获取车辆状态
    CarState* state = m_carStates->getState(info.car_id);
    if (!state) {
        return;
    }
    
    // 检查请求是否仍然是当前请求(Latest-Only)
    if (!state->isCurrentRequest(result.request_id)) {
        SYLAR_LOG_DEBUG(g_logger) << "Stale result dropped: " << result.request_id;
        m_carStates->unregisterRequest(result.request_id);
        return;
    }
    
    // 完成请求
    state->finishRequest(result.request_id);
    m_carStates->unregisterRequest(result.request_id);
    
    // 记录结果
    uint64_t now = getCurrentTimeMs();
    if (result.status == VllmResult::Status::SUCCESS) {
        m_metrics->recordSuccess(info.car_id);
        
        // 记录端到端延迟
        uint64_t e2e_latency = now - info.device_timestamp_ms;
        m_metrics->recordE2ELatency(e2e_latency);
        
        // 构建输出消息
        OutputMessage output;
        output.car_id = info.car_id;
        output.seq = info.seq;
        output.status = "success";
        output.latency_ms = e2e_latency;
        output.waypoints = parseWaypoints(result.output_text);
        
        // 发送结果
        {
            Mutex::Lock lock(m_callbackMutex);
            if (m_resultCallback) {
                m_resultCallback(info.car_id, output);
            }
        }
        
        SYLAR_LOG_DEBUG(g_logger) << "Inference complete: request_id=" 
                                   << result.request_id
                                   << " car_id=" << info.car_id
                                   << " latency=" << e2e_latency << "ms";
    } else if (result.status == VllmResult::Status::ERROR) {
        m_metrics->recordError(info.car_id);
        SYLAR_LOG_ERROR(g_logger) << "Inference error: " << result.error_message
                                   << " request_id=" << result.request_id;
    } else if (result.status == VllmResult::Status::TIMEOUT) {
        m_metrics->recordTimeout(info.car_id);
        SYLAR_LOG_WARN(g_logger) << "Inference timeout: request_id=" 
                                  << result.request_id;
    }
}

std::string IcpController::parseWaypoints(const std::string& output_text) {
    // 尝试从输出文本中提取waypoints
    // 格式: [[x1,y1],[x2,y2],...] 或 {"waypoints":[[x1,y1],...]}
    
    // 简单实现: 查找 [ 开始的数组
    size_t start = output_text.find('[');
    if (start == std::string::npos) {
        // 没有找到waypoints, 返回空数组
        return "[]";
    }
    
    // 找到匹配的 ]
    int depth = 0;
    size_t end = start;
    for (size_t i = start; i < output_text.size(); ++i) {
        if (output_text[i] == '[') {
            depth++;
        } else if (output_text[i] == ']') {
            depth--;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    
    if (end > start) {
        return output_text.substr(start, end - start + 1);
    }
    
    return "[]";
}

} // namespace icp
} // namespace sherry
