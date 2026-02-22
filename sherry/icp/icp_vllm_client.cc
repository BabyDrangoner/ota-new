#include "icp_vllm_client.h"
#include "icp_car_state.h"
#include "sherry/log.h"
#include "sherry/http/http_connection.h"
#include "sherry/uri.h"

#include <sstream>
#include <algorithm>

namespace sherry {
namespace icp {

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("icp");

//------------------------------------------------------------------------------
// Base64 编码
//------------------------------------------------------------------------------

static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64Encode(const uint8_t* data, size_t size) {
    std::string result;
    result.reserve(((size + 2) / 3) * 4);
    
    for (size_t i = 0; i < size; i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16);
        if (i + 1 < size) n |= (static_cast<uint32_t>(data[i + 1]) << 8);
        if (i + 2 < size) n |= static_cast<uint32_t>(data[i + 2]);
        
        result.push_back(BASE64_CHARS[(n >> 18) & 0x3F]);
        result.push_back(BASE64_CHARS[(n >> 12) & 0x3F]);
        
        if (i + 1 < size) {
            result.push_back(BASE64_CHARS[(n >> 6) & 0x3F]);
        } else {
            result.push_back('=');
        }
        
        if (i + 2 < size) {
            result.push_back(BASE64_CHARS[n & 0x3F]);
        } else {
            result.push_back('=');
        }
    }
    
    return result;
}

std::vector<std::string> encodeImagesToBase64(const std::vector<ImageView>& images) {
    std::vector<std::string> result;
    result.reserve(images.size());
    
    for (const auto& img : images) {
        result.push_back(base64Encode(img.data, img.size));
    }
    
    return result;
}

//------------------------------------------------------------------------------
// VllmClient
//------------------------------------------------------------------------------

VllmClient::VllmClient(const VllmConfig& config,
                       IcpMetrics::ptr metrics,
                       VllmCallback* callback)
    : m_config(config)
    , m_metrics(metrics)
    , m_callback(callback)
    , m_ioManager(nullptr) {
}

VllmClient::~VllmClient() {
    stop();
}

bool VllmClient::init(IOManager* io_manager) {
    if (!io_manager) {
        SYLAR_LOG_ERROR(g_logger) << "VllmClient::init - IOManager is null";
        return false;
    }
    
    m_ioManager = io_manager;
    m_running.store(true, std::memory_order_release);
    
    SYLAR_LOG_INFO(g_logger) << "VllmClient initialized with endpoint: " 
                              << m_config.endpoint
                              << " model: " << m_config.model;
    return true;
}

void VllmClient::submit(VllmRequest::ptr request) {
    if (!m_running.load(std::memory_order_acquire)) {
        SYLAR_LOG_WARN(g_logger) << "VllmClient::submit - client not running";
        return;
    }
    
    markInflight(request->request_id);
    
    // 在IO线程中异步执行HTTP请求
    m_ioManager->schedule([this, request]() {
        doHttpRequest(request);
    });
}

void VllmClient::abort(const std::string& request_id) {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    
    // 标记为已abort
    {
        Mutex::Lock lock(m_abortedMutex);
        m_abortedRequests.insert(request_id);
    }
    
    // 如果请求正在处理, 发送abort请求
    if (isInflight(request_id)) {
        m_ioManager->schedule([this, request_id]() {
            doHttpAbort(request_id);
        });
    }
}

bool VllmClient::isInflight(const std::string& request_id) const {
    RWMutex::ReadLock lock(m_inflightMutex);
    return m_inflightRequests.count(request_id) > 0;
}

size_t VllmClient::getInflightCount() const {
    RWMutex::ReadLock lock(m_inflightMutex);
    return m_inflightRequests.size();
}

void VllmClient::stop() {
    m_running.store(false, std::memory_order_release);
    
    // 清理资源
    {
        RWMutex::WriteLock lock(m_inflightMutex);
        m_inflightRequests.clear();
    }
    {
        Mutex::Lock lock(m_abortedMutex);
        m_abortedRequests.clear();
    }
}

std::string VllmClient::buildRequestBody(const VllmRequest& request) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"model\": \"" << m_config.model << "\",\n";
    oss << "  \"max_tokens\": " << m_config.max_tokens << ",\n";
    oss << "  \"temperature\": " << m_config.temperature << ",\n";
    oss << "  \"top_p\": " << m_config.top_p << ",\n";
    oss << "  \"repetition_penalty\": " << m_config.repetition_penalty << ",\n";
    oss << "  \"stream\": " << (m_config.enable_stream ? "true" : "false") << ",\n";
    
    // 构建messages数组 (Qwen3-VL OpenAI Vision API格式)
    oss << "  \"messages\": [\n";
    
    // system 消息
    if (!m_config.system_prompt.empty()) {
        oss << "    {\n";
        oss << "      \"role\": \"system\",\n";
        oss << "      \"content\": \"" << m_config.system_prompt << "\"\n";
        oss << "    },\n";
    }
    
    // user 消息
    oss << "    {\n";
    oss << "      \"role\": \"user\",\n";
    oss << "      \"content\": [\n";
    
    // 添加图片 (Qwen3-VL格式: image_url 带 min_pixels/max_pixels)
    for (size_t i = 0; i < request.images_base64.size(); ++i) {
        oss << "        {\n";
        oss << "          \"type\": \"image_url\",\n";
        oss << "          \"image_url\": {\n";
        oss << "            \"url\": \"data:image/jpeg;base64," 
            << request.images_base64[i] << "\",\n";
        oss << "            \"min_pixels\": " << m_config.min_pixels << ",\n";
        oss << "            \"max_pixels\": " << m_config.max_pixels << "\n";
        oss << "          }\n";
        oss << "        }";
        if (i < request.images_base64.size() - 1 || !request.prompt.empty()) {
            oss << ",";
        }
        oss << "\n";
    }
    
    // 添加文本提示
    if (!request.prompt.empty()) {
        oss << "        {\n";
        oss << "          \"type\": \"text\",\n";
        oss << "          \"text\": \"" << request.prompt << "\"\n";
        oss << "        }\n";
    }
    
    oss << "      ]\n";
    oss << "    }\n";
    oss << "  ]\n";
    oss << "}";
    
    return oss.str();
}

void VllmClient::doHttpRequest(VllmRequest::ptr request) {
    uint64_t start_time = getCurrentTimeMs();
    
    // 检查是否已被abort
    {
        Mutex::Lock lock(m_abortedMutex);
        if (m_abortedRequests.count(request->request_id) > 0) {
            unmarkInflight(request->request_id);
            return;
        }
    }
    
    // 构建请求体
    std::string body = buildRequestBody(*request);
    
    // 构建URL
    std::string url = m_config.endpoint + "/v1/chat/completions";
    
    // 设置请求头
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = m_config.enable_stream ? 
        "text/event-stream" : "application/json";
    
    // 发送请求
    if (m_metrics) {
        m_metrics->getSystemMetrics().http_requests.fetch_add(1, 
            std::memory_order_relaxed);
    }
    
    auto result = http::HttpConnection::DoPost(url, m_config.timeout_ms, 
                                                headers, body);
    
    // 检查是否已被abort
    {
        Mutex::Lock lock(m_abortedMutex);
        if (m_abortedRequests.count(request->request_id) > 0) {
            m_abortedRequests.erase(request->request_id);
            unmarkInflight(request->request_id);
            return;
        }
    }
    
    VllmResult vllm_result;
    vllm_result.request_id = request->request_id;
    vllm_result.complete_time_ms = getCurrentTimeMs();
    
    if (result->result != 0) {
        // 请求失败
        vllm_result.status = VllmResult::Status::ERROR;
        vllm_result.error_message = result->error;
        
        SYLAR_LOG_ERROR(g_logger) << "VllmClient HTTP request failed: " 
                                   << result->error
                                   << " request_id=" << request->request_id;
        
        if (m_metrics) {
            m_metrics->getSystemMetrics().http_errors.fetch_add(1, 
                std::memory_order_relaxed);
        }
    } else if (result->response->getStatus() != http::HttpStatus::OK) {
        // HTTP错误状态
        vllm_result.status = VllmResult::Status::ERROR;
        vllm_result.error_message = "HTTP " + 
            std::to_string(static_cast<int>(result->response->getStatus()));
        
        SYLAR_LOG_ERROR(g_logger) << "VllmClient HTTP error: " 
                                   << vllm_result.error_message
                                   << " request_id=" << request->request_id;
        
        if (m_metrics) {
            m_metrics->getSystemMetrics().http_errors.fetch_add(1, 
                std::memory_order_relaxed);
        }
    } else {
        // 成功
        vllm_result = parseResponse(request->request_id, 
                                    result->response->getBody());
    }
    
    // 记录延迟
    if (m_metrics && vllm_result.status == VllmResult::Status::SUCCESS) {
        uint64_t latency = vllm_result.complete_time_ms - start_time;
        m_metrics->recordSubmitToDoneLatency(latency);
    }
    
    // 回调
    unmarkInflight(request->request_id);
    
    if (m_callback) {
        m_callback->onComplete(vllm_result);
    }
}

void VllmClient::doHttpAbort(const std::string& request_id) {
    // 尝试发送abort请求到vLLM(如果支持)
    // 注意: 标准OpenAI API不支持abort, 但vLLM可能有额外接口
    
    std::string url = m_config.endpoint + "/v1/abort";
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    
    std::ostringstream body;
    body << "{\"request_id\":\"" << request_id << "\"}";
    
    auto result = http::HttpConnection::DoPost(url, 5000, headers, body.str());
    
    if (result->result == 0 && 
        result->response->getStatus() == http::HttpStatus::OK) {
        SYLAR_LOG_DEBUG(g_logger) << "VllmClient abort sent: " << request_id;
    }
    
    // 无论abort是否成功, 都要清理inflight状态
    unmarkInflight(request_id);
}

VllmResult VllmClient::parseResponse(const std::string& request_id,
                                      const std::string& response_body) {
    VllmResult result;
    result.request_id = request_id;
    result.complete_time_ms = getCurrentTimeMs();
    
    // 解析 vLLM OpenAI-compatible 响应格式 (Qwen3-VL-4B)
    // 标准响应结构:
    // {
    //   "choices": [{
    //     "message": { "content": "..." },
    //     "finish_reason": "stop"
    //   }],
    //   "usage": { "completion_tokens": N, ... }
    // }
    
    SYLAR_LOG_DEBUG(g_logger) << "VllmClient parseResponse body (first 500 chars): "
                               << response_body.substr(0, 500);
    
    // 1. 先尝试解析 "choices" -> "message" -> "content" (标准 chat completion 格式)
    bool found_content = false;
    size_t choices_pos = response_body.find("\"choices\"");
    if (choices_pos != std::string::npos) {
        // 在 choices 内查找 message.content
        size_t message_pos = response_body.find("\"message\"", choices_pos);
        if (message_pos != std::string::npos) {
            size_t content_pos = response_body.find("\"content\"", message_pos);
            if (content_pos != std::string::npos) {
                // 跳过 "content": 找到值的开头
                size_t colon_pos = response_body.find(':', content_pos + 9);
                if (colon_pos != std::string::npos) {
                    // 跳过空白
                    size_t val_start = colon_pos + 1;
                    while (val_start < response_body.size() && 
                           (response_body[val_start] == ' ' || response_body[val_start] == '\n' ||
                            response_body[val_start] == '\r' || response_body[val_start] == '\t')) {
                        val_start++;
                    }
                    
                    if (val_start < response_body.size()) {
                        if (response_body[val_start] == '"') {
                            // 字符串值 - 提取内容, 处理转义字符
                            size_t end = val_start + 1;
                            while (end < response_body.size()) {
                                if (response_body[end] == '\\') {
                                    end += 2;  // 跳过转义字符
                                } else if (response_body[end] == '"') {
                                    break;
                                } else {
                                    ++end;
                                }
                            }
                            result.output_text = response_body.substr(val_start + 1, end - val_start - 1);
                            found_content = true;
                        } else if (response_body.compare(val_start, 4, "null") == 0) {
                            // content 为 null (thinking 模式可能出现)
                            result.output_text = "";
                            found_content = true;
                        }
                    }
                }
            }
        }
    }
    
    // 2. 回退: 直接搜索 "content" 字段
    if (!found_content) {
        size_t content_pos = response_body.find("\"content\":");
        if (content_pos != std::string::npos) {
            size_t start = response_body.find("\"", content_pos + 10);
            if (start != std::string::npos) {
                size_t end = start + 1;
                while (end < response_body.size()) {
                    if (response_body[end] == '\\') {
                        end += 2;
                    } else if (response_body[end] == '"') {
                        break;
                    } else {
                        ++end;
                    }
                }
                result.output_text = response_body.substr(start + 1, end - start - 1);
                found_content = true;
            }
        }
    }
    
    // 3. 尝试提取 usage.completion_tokens 作为 token_count
    size_t usage_pos = response_body.find("\"completion_tokens\"");
    if (usage_pos != std::string::npos) {
        size_t colon = response_body.find(':', usage_pos + 19);
        if (colon != std::string::npos) {
            size_t num_start = colon + 1;
            while (num_start < response_body.size() && 
                   (response_body[num_start] == ' ' || response_body[num_start] == '\n')) {
                num_start++;
            }
            std::string num_str;
            while (num_start < response_body.size() && 
                   response_body[num_start] >= '0' && response_body[num_start] <= '9') {
                num_str += response_body[num_start++];
            }
            if (!num_str.empty()) {
                result.token_count = static_cast<uint32_t>(std::stoul(num_str));
            }
        }
    }
    
    if (found_content) {
        result.status = VllmResult::Status::SUCCESS;
        SYLAR_LOG_DEBUG(g_logger) << "VllmClient parsed content (first 200 chars): " 
                                   << result.output_text.substr(0, 200)
                                   << " tokens=" << result.token_count;
    } else {
        // 尝试查找错误信息
        size_t error_pos = response_body.find("\"error\"");
        if (error_pos != std::string::npos) {
            // 提取 error.message
            size_t msg_pos = response_body.find("\"message\"", error_pos);
            if (msg_pos != std::string::npos) {
                size_t start = response_body.find("\"", msg_pos + 9);
                if (start != std::string::npos) {
                    size_t end = start + 1;
                    while (end < response_body.size()) {
                        if (response_body[end] == '\\') {
                            end += 2;
                        } else if (response_body[end] == '"') {
                            break;
                        } else {
                            ++end;
                        }
                    }
                    result.error_message = response_body.substr(start + 1, end - start - 1);
                }
            } else {
                result.error_message = "API error in response";
            }
            result.status = VllmResult::Status::ERROR;
            SYLAR_LOG_ERROR(g_logger) << "VllmClient API error: " << result.error_message;
        } else {
            // 无法解析但也无错误, 标记成功但空内容
            result.status = VllmResult::Status::SUCCESS;
            result.output_text = "";
        }
    }
    
    return result;
}

void VllmClient::markInflight(const std::string& request_id) {
    RWMutex::WriteLock lock(m_inflightMutex);
    m_inflightRequests.insert(request_id);
}

void VllmClient::unmarkInflight(const std::string& request_id) {
    RWMutex::WriteLock lock(m_inflightMutex);
    m_inflightRequests.erase(request_id);
}

} // namespace icp
} // namespace sherry
