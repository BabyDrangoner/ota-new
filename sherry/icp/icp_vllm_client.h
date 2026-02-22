#ifndef __SHERRY_ICP_VLLM_CLIENT_H__
#define __SHERRY_ICP_VLLM_CLIENT_H__

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <unordered_set>

#include "icp_config.h"
#include "icp_protocol.h"
#include "icp_metrics.h"
#include "sherry/thread.h"
#include "sherry/iomanager.h"

namespace sherry {
namespace icp {

/**
 * @brief vLLM 推理请求
 */
struct VllmRequest {
    typedef std::shared_ptr<VllmRequest> ptr;
    
    std::string request_id;                    // 请求ID
    std::string prompt;                        // 文本提示
    std::vector<std::string> images_base64;    // Base64编码的图片
    uint32_t car_id;                           // 车辆ID
    uint64_t seq;                              // 序列号
    uint64_t device_timestamp_ms;              // 设备时间戳
    uint64_t submit_time_ms;                   // 提交时间
};

/**
 * @brief vLLM 推理结果
 */
struct VllmResult {
    typedef std::shared_ptr<VllmResult> ptr;
    
    enum class Status {
        SUCCESS = 0,
        ERROR = 1,
        TIMEOUT = 2,
        ABORTED = 3
    };
    
    std::string request_id;
    Status status;
    std::string output_text;                   // 输出文本
    std::string error_message;                 // 错误信息
    
    uint64_t first_token_time_ms;              // 首token时间
    uint64_t complete_time_ms;                 // 完成时间
    uint32_t token_count;                      // token数量
};

/**
 * @brief vLLM 回调接口
 */
class VllmCallback {
public:
    virtual ~VllmCallback() = default;
    
    /**
     * @brief 推理完成回调
     */
    virtual void onComplete(const VllmResult& result) = 0;
    
    /**
     * @brief 首token到达回调(用于流式)
     */
    virtual void onFirstToken(const std::string& request_id, 
                               uint64_t time_ms) {}
    
    /**
     * @brief 流式token回调
     */
    virtual void onStreamToken(const std::string& request_id,
                                const std::string& token) {}
};

/**
 * @brief vLLM HTTP 客户端适配器
 * 
 * 职责:
 * - OpenAI-compatible HTTP 发送
 * - submit(request_id, prompt, images[])
 * - abort(request_id)
 * - 处理 streaming
 * - 绝不阻塞 control 线程
 */
class VllmClient {
public:
    typedef std::shared_ptr<VllmClient> ptr;
    
    /**
     * @brief 构造函数
     * @param config vLLM配置
     * @param metrics 指标收集器
     * @param callback 结果回调
     */
    VllmClient(const VllmConfig& config, 
               IcpMetrics::ptr metrics,
               VllmCallback* callback);
    
    ~VllmClient();
    
    /**
     * @brief 初始化客户端
     * @param io_manager IOManager用于异步HTTP请求
     * @return true表示成功
     */
    bool init(IOManager* io_manager);
    
    /**
     * @brief 提交推理请求(异步, 不阻塞)
     * @param request 推理请求
     */
    void submit(VllmRequest::ptr request);
    
    /**
     * @brief 取消推理请求(异步, 不阻塞)
     * @param request_id 请求ID
     */
    void abort(const std::string& request_id);
    
    /**
     * @brief 检查请求是否正在处理
     */
    bool isInflight(const std::string& request_id) const;
    
    /**
     * @brief 获取正在处理的请求数
     */
    size_t getInflightCount() const;
    
    /**
     * @brief 停止客户端
     */
    void stop();
    
private:
    /**
     * @brief 构建OpenAI兼容的请求体
     */
    std::string buildRequestBody(const VllmRequest& request);
    
    /**
     * @brief 执行HTTP请求(在IO线程中执行)
     */
    void doHttpRequest(VllmRequest::ptr request);
    
    /**
     * @brief 执行HTTP abort请求
     */
    void doHttpAbort(const std::string& request_id);
    
    /**
     * @brief 解析流式响应
     */
    void parseStreamResponse(const std::string& request_id,
                              const std::string& chunk);
    
    /**
     * @brief 解析非流式响应  
     */
    VllmResult parseResponse(const std::string& request_id,
                              const std::string& response_body);
    
    /**
     * @brief 标记请求为inflight
     */
    void markInflight(const std::string& request_id);
    
    /**
     * @brief 取消标记inflight
     */
    void unmarkInflight(const std::string& request_id);
    
private:
    VllmConfig m_config;
    IcpMetrics::ptr m_metrics;
    VllmCallback* m_callback;
    IOManager* m_ioManager;
    
    // Inflight请求跟踪
    mutable RWMutex m_inflightMutex;
    std::unordered_set<std::string> m_inflightRequests;
    
    // 已abort的请求(用于过滤晚到的结果)
    mutable Mutex m_abortedMutex;
    std::unordered_set<std::string> m_abortedRequests;
    
    std::atomic<bool> m_running{false};
};

/**
 * @brief Base64 编码工具函数
 */
std::string base64Encode(const uint8_t* data, size_t size);

/**
 * @brief 从ImageView列表编码为Base64字符串列表
 */
std::vector<std::string> encodeImagesToBase64(const std::vector<ImageView>& images);

} // namespace icp
} // namespace sherry

#endif // __SHERRY_ICP_VLLM_CLIENT_H__
