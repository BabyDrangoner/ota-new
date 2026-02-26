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

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    // 控制字符: \uXXXX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
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

    // 创建 HTTP 连接池，避免每次推理都新建 TCP 连接
    m_pool = http::HttpConnectionPool::Create(
        m_config.endpoint,
        "",
        m_config.max_connections,  // 最大连接数
        5 * 60 * 1000,             // 最大存活时间：5 分钟
        100000                     // 每条连接最大请求数
    );

    // 缓存 Host header（含端口，如 localhost:8000）
    sherry::Uri::ptr uri = sherry::Uri::Create(m_config.endpoint);
    if (uri) {
        m_vllmHost = uri->getHost();
        uint16_t port = uri->getPort();
        bool is_https = uri->getScheme() == "https";
        if (port && port != (is_https ? 443 : 80)) {
            m_vllmHost += ":" + std::to_string(port);
        }
    }

    SYLAR_LOG_INFO(g_logger) << "VllmClient initialized with endpoint: " 
                              << m_config.endpoint
                              << " model: " << m_config.model
                              << " pool_size: " << m_config.max_connections;
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
        oss << "      \"content\": \"" << jsonEscape(m_config.system_prompt) << "\"\n";
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
        // base64 字符集本身不含需转义的字符, 无需 jsonEscape
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
        oss << "          \"text\": \"" << jsonEscape(request.prompt) << "\"\n";
        oss << "        }\n";
    }
    
    oss << "      ]\n";
    oss << "    }\n";
    oss << "  ]\n";
    oss << "}";
    
    return oss.str();
}

void VllmClient::doHttpRequest(VllmRequest::ptr request) {
    // 流式模式走独立实现
    if (m_config.enable_stream) {
        doHttpRequestStream(request);
        return;
    }

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
    
    SYLAR_LOG_DEBUG(g_logger) << "VllmClient request body (first 1000 chars): "
                               << body.substr(0, 1000);
    
    // 设置请求头
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json";

    // 发送请求（通过连接池，支持 keep-alive 复用）
    if (m_metrics) {
        m_metrics->getSystemMetrics().http_requests.fetch_add(1, 
            std::memory_order_relaxed);
    }

    auto result = m_pool->doPost("/v1/chat/completions",
                                  m_config.timeout_ms, headers, body);
    
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
        
        // 打印响应体辅助排查 400/422 等错误
        const std::string& resp_body = result->response->getBody();
        SYLAR_LOG_ERROR(g_logger) << "VllmClient HTTP error: " 
                                   << vllm_result.error_message
                                   << " request_id=" << request->request_id
                                   << " response_body=" 
                                   << resp_body.substr(0, 512);
        
        if (m_metrics) {
            m_metrics->getSystemMetrics().http_errors.fetch_add(1, 
                std::memory_order_relaxed);
        }
    } else {
        // 成功
        if (m_config.enable_stream) {
            vllm_result = parseStreamResponse(request->request_id,
                                              result->response->getBody());
        } else {
            vllm_result = parseResponse(request->request_id,
                                        result->response->getBody());
        }
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

VllmResult VllmClient::parseStreamResponse(const std::string& request_id,
                                           const std::string& body) {
    // vLLM SSE 格式:
    //   data: {"choices":[{"delta":{"content":"token"},"finish_reason":null}],...}\n\n
    //   data: [DONE]\n\n
    //
    // 逐行扫描, 提取 delta.content, 触发 onStreamToken 回调;
    // 最终拼接全文后触发 onComplete.

    VllmResult result;
    result.request_id        = request_id;
    result.complete_time_ms  = getCurrentTimeMs();
    result.token_count       = 0;

    std::string full_text;
    bool first_token = true;

    SYLAR_LOG_DEBUG(g_logger) << "parseStreamResponse: body size=" << body.size();

    size_t pos = 0;
    while (pos < body.size()) {
        // 读取一行
        size_t nl = body.find('\n', pos);
        size_t line_end = (nl == std::string::npos) ? body.size() : nl;
        std::string line = body.substr(pos, line_end - pos);
        pos = (nl == std::string::npos) ? body.size() : nl + 1;

        // 去掉尾部 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        // SSE 行格式: "data: <payload>"
        if (line.rfind("data: ", 0) != 0) continue;
        std::string payload = line.substr(6);  // 去掉 "data: "

        if (payload == "[DONE]") break;

        // 从 payload (JSON) 中提取 choices[0].delta.content
        // 使用手工字符串查找，避免引入 JSON 库依赖
        std::string token;
        size_t delta_pos = payload.find("\"delta\"");
        if (delta_pos != std::string::npos) {
            size_t content_pos = payload.find("\"content\"", delta_pos);
            if (content_pos != std::string::npos) {
                size_t colon = payload.find(':', content_pos + 9);
                if (colon != std::string::npos) {
                    size_t val = colon + 1;
                    while (val < payload.size() &&
                           (payload[val] == ' ' || payload[val] == '\t')) ++val;
                    if (val < payload.size() && payload[val] == '"') {
                        size_t end = val + 1;
                        while (end < payload.size()) {
                            if (payload[end] == '\\') { end += 2; }
                            else if (payload[end] == '"') { break; }
                            else { ++end; }
                        }
                        token = payload.substr(val + 1, end - val - 1);
                    }
                }
            }
        }

        if (token.empty()) continue;

        // 首 token: 记录时间并回调
        if (first_token) {
            first_token = false;
            uint64_t ft_time = getCurrentTimeMs();
            if (m_callback) {
                m_callback->onFirstToken(request_id, ft_time);
            }
        }

        // 处理转义序列 (\n \t \r \\)
        std::string decoded;
        for (size_t i = 0; i < token.size(); ++i) {
            if (token[i] == '\\' && i + 1 < token.size()) {
                char nc = token[i + 1];
                if      (nc == 'n')  { decoded += '\n'; ++i; }
                else if (nc == 't')  { decoded += '\t'; ++i; }
                else if (nc == 'r')  { decoded += '\r'; ++i; }
                else if (nc == '\\') { decoded += '\\'; ++i; }
                else if (nc == '"')  { decoded += '"';  ++i; }
                else                 { decoded += nc;    ++i; }
            } else {
                decoded += token[i];
            }
        }

        full_text += decoded;
        ++result.token_count;

        if (m_callback) {
            m_callback->onStreamToken(request_id, decoded);
        }
    }

    result.output_text = full_text;
    result.status      = full_text.empty()
                         ? VllmResult::Status::ERROR
                         : VllmResult::Status::SUCCESS;
    if (result.status == VllmResult::Status::ERROR) {
        result.error_message = "stream response: no content parsed";
        SYLAR_LOG_ERROR(g_logger) << "parseStreamResponse: no content, body="
                                   << body.substr(0, 256);
    } else {
        SYLAR_LOG_DEBUG(g_logger) << "parseStreamResponse done: tokens="
                                   << result.token_count
                                   << " text_len=" << full_text.size();
    }
    return result;
}

bool VllmClient::parseTokensFromBatch(const std::string& chunk_data,
                                       std::vector<std::string>& out_tokens) {
    // chunk_data 内容为若干行 SSE 数据，格式如：
    //   data: {JSON}\n\n
    //   data: [DONE]\n\n
    bool hit_done = false;
    size_t pos = 0;
    while (pos < chunk_data.size()) {
        size_t nl  = chunk_data.find('\n', pos);
        size_t end = (nl == std::string::npos) ? chunk_data.size() : nl;
        std::string line = chunk_data.substr(pos, end - pos);
        pos = (nl == std::string::npos) ? chunk_data.size() : nl + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("data: ", 0) != 0) continue;

        std::string payload = line.substr(6);
        if (payload == "[DONE]") { hit_done = true; break; }

        // 提取 choices[0].delta.content
        size_t delta_pos = payload.find("\"delta\"");
        if (delta_pos == std::string::npos) continue;
        size_t content_pos = payload.find("\"content\"", delta_pos);
        if (content_pos == std::string::npos) continue;
        size_t colon = payload.find(':', content_pos + 9);
        if (colon == std::string::npos) continue;
        size_t val = colon + 1;
        while (val < payload.size() &&
               (payload[val] == ' ' || payload[val] == '\t')) ++val;
        if (val >= payload.size() || payload[val] != '"') continue;
        size_t str_end = val + 1;
        while (str_end < payload.size()) {
            if (payload[str_end] == '\\') { str_end += 2; }
            else if (payload[str_end] == '"') { break; }
            else { ++str_end; }
        }
        std::string raw_token = payload.substr(val + 1, str_end - val - 1);
        if (raw_token.empty()) continue;

        // 处理 JSON 转义
        std::string token;
        for (size_t i = 0; i < raw_token.size(); ++i) {
            if (raw_token[i] == '\\' && i + 1 < raw_token.size()) {
                char nc = raw_token[i + 1];
                if      (nc == 'n')  { token += '\n'; ++i; }
                else if (nc == 't')  { token += '\t'; ++i; }
                else if (nc == 'r')  { token += '\r'; ++i; }
                else if (nc == '\\') { token += '\\'; ++i; }
                else if (nc == '"')  { token += '"';  ++i; }
                else                 { token += nc;    ++i; }
            } else {
                token += raw_token[i];
            }
        }
        out_tokens.push_back(std::move(token));
    }
    return hit_done;
}

void VllmClient::doHttpRequestStream(VllmRequest::ptr request) {
    uint64_t start_time = getCurrentTimeMs();

    // 检查是否已被 abort
    {
        Mutex::Lock lock(m_abortedMutex);
        if (m_abortedRequests.count(request->request_id) > 0) {
            unmarkInflight(request->request_id);
            return;
        }
    }

    std::string body = buildRequestBody(*request);

    if (m_metrics) {
        m_metrics->getSystemMetrics().http_requests.fetch_add(1,
            std::memory_order_relaxed);
    }

    // 流式状态
    const std::string req_id   = request->request_id;
    std::string       full_text;
    uint32_t          token_count = 0;
    bool              first_token = true;
    bool              stream_done = false;

    // 按配置的批次大小聚合 chunk 后解析
    http::HttpConnection::ChunkPolicy policy;
    policy.max_chunks = m_config.stream_batch_chunks;  // 0=无限制 1=逐chunk N=批次N

    auto chunk_cb = [&](const std::string& data, bool is_done) {
        // 检查 abort
        {
            Mutex::Lock lock(m_abortedMutex);
            if (m_abortedRequests.count(req_id) > 0) {
                stream_done = true;
                return;
            }
        }

        if (!data.empty()) {
            std::vector<std::string> tokens;
            bool hit_done = parseTokensFromBatch(data, tokens);

            for (auto& tok : tokens) {
                if (first_token) {
                    first_token = false;
                    uint64_t ft_time = getCurrentTimeMs();
                    SYLAR_LOG_INFO(g_logger)
                        << "[stream] first_token request_id=" << req_id
                        << " ttft_ms=" << (ft_time - start_time);
                    if (m_callback) {
                        m_callback->onFirstToken(req_id, ft_time);
                    }
                }

                full_text += tok;
                ++token_count;

                SYLAR_LOG_INFO(g_logger)
                    << "[stream] token #" << token_count
                    << " request_id=" << req_id
                    << " token=[" << tok << "]";

                if (m_callback) {
                    m_callback->onStreamToken(req_id, tok);
                }
            }

            // 批次回调：一次把这批所有 token 一起通知
            if (m_callback && !tokens.empty()) {
                m_callback->onStreamBatch(req_id, tokens);
            }

            if (hit_done) stream_done = true;
        }

        if (is_done) stream_done = true;
    };

    // 从连接池获取连接，避免每次推理都新建 TCP 握手
    http::HttpResult::ptr result;
    auto conn = m_pool->getConnection();
    if (!conn) {
        result = std::make_shared<http::HttpResult>(
            (int)http::HttpResult::Error::POOL_GET_CONNECTION,
            nullptr, "get connection from pool failed: " + m_config.endpoint);
    } else {
        conn->getSocket()->setRecvTimeout(m_config.timeout_ms);

        http::HttpRequest::ptr req = std::make_shared<http::HttpRequest>();
        req->setPath("/v1/chat/completions");
        req->setMethod(http::HttpMethod::POST);
        req->setClose(false);  // keep-alive：请求结束后连接归还池
        req->setHeader("Host",         m_vllmHost);
        req->setHeader("Content-Type", "application/json");
        req->setHeader("Accept",       "text/event-stream");
        req->setBody(body);

        int rt = conn->sendRequest(req);
        if (rt <= 0) {
            result = std::make_shared<http::HttpResult>(
                (int)http::HttpResult::Error::SEND_SOCKET_ERROR,
                nullptr, "send request failed");
        } else {
            auto rsp = conn->recvResponseStream(policy, chunk_cb);
            result = rsp
                ? std::make_shared<http::HttpResult>((int)http::HttpResult::Error::OK, rsp, "ok")
                : std::make_shared<http::HttpResult>((int)http::HttpResult::Error::TIMEOUT,
                                                     nullptr, "recv stream timeout: " + m_config.endpoint);
        }
        // conn 析构时自动归还连接池（若 socket 仍活着）
    }

    // 检查 abort
    {
        Mutex::Lock lock(m_abortedMutex);
        if (m_abortedRequests.count(req_id) > 0) {
            m_abortedRequests.erase(req_id);
            unmarkInflight(req_id);
            return;
        }
    }

    VllmResult vllm_result;
    vllm_result.request_id      = req_id;
    vllm_result.complete_time_ms = getCurrentTimeMs();

    if (!result || result->result != 0) {
        vllm_result.status        = VllmResult::Status::ERROR;
        vllm_result.error_message = result ? result->error : "DoPostStream failed";
        SYLAR_LOG_ERROR(g_logger) << "[stream] HTTP failed: " << vllm_result.error_message
                                   << " request_id=" << req_id;
        if (m_metrics) {
            m_metrics->getSystemMetrics().http_errors.fetch_add(1,
                std::memory_order_relaxed);
        }
    } else if (result->response->getStatus() != http::HttpStatus::OK) {
        vllm_result.status        = VllmResult::Status::ERROR;
        vllm_result.error_message = "HTTP " +
            std::to_string(static_cast<int>(result->response->getStatus()));
        SYLAR_LOG_ERROR(g_logger) << "[stream] HTTP error: " << vllm_result.error_message
                                   << " request_id=" << req_id;
        if (m_metrics) {
            m_metrics->getSystemMetrics().http_errors.fetch_add(1,
                std::memory_order_relaxed);
        }
    } else {
        vllm_result.output_text = full_text;
        vllm_result.token_count = token_count;
        vllm_result.status      = full_text.empty()
                                   ? VllmResult::Status::ERROR
                                   : VllmResult::Status::SUCCESS;
        if (vllm_result.status == VllmResult::Status::ERROR) {
            vllm_result.error_message = "stream: no content";
        }
        SYLAR_LOG_INFO(g_logger)
            << "[stream] done request_id=" << req_id
            << " tokens=" << token_count
            << " text_len=" << full_text.size()
            << " latency_ms=" << (vllm_result.complete_time_ms - start_time);
    }

    if (m_metrics && vllm_result.status == VllmResult::Status::SUCCESS) {
        uint64_t latency = vllm_result.complete_time_ms - start_time;
        m_metrics->recordSubmitToDoneLatency(latency);
    }

    unmarkInflight(req_id);
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
