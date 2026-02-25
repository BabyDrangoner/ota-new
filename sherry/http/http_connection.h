#ifndef __SHERRY_HTTP_CONNECTION_H__
#define __SHERRY_HTTP_CONNECTION_H__

#include "sherry/socket_stream.h"
#include "http.h"
#include "sherry/uri.h"
#include "sherry/thread.h"

#include <functional>
#include <list>

namespace sherry {
namespace http {

/**
 * @brief HTTP响应结果
 */
struct HttpResult {
    /// 智能指针类型定义
    typedef std::shared_ptr<HttpResult> ptr;
    /**
     * @brief 错误码定义
     */
    enum class Error {
        /// 正常
        OK = 0,
        /// 非法URL
        INVALID_URL = 1,
        /// 无法解析HOST
        INVALID_HOST = 2,
        /// 连接失败
        CONNECT_FAIL = 3,
        /// 连接被对端关闭
        SEND_CLOSE_BY_PEER = 4,
        /// 发送请求产生Socket错误
        SEND_SOCKET_ERROR = 5,
        /// 超时
        TIMEOUT = 6,
        /// 创建Socket失败
        CREATE_SOCKET_ERROR = 7,
        /// 从连接池中取连接失败
        POOL_GET_CONNECTION = 8,
        /// 无效的连接
        POOL_INVALID_CONNECTION = 9,
    };

    /**
     * @brief 构造函数
     * @param[in] _result 错误码
     * @param[in] _response HTTP响应结构体
     * @param[in] _error 错误描述
     */
    HttpResult(int _result
               ,HttpResponse::ptr _response
               ,const std::string& _error)
        :result(_result)
        ,response(_response)
        ,error(_error) {}

    /// 错误码
    int result;
    /// HTTP响应结构体
    HttpResponse::ptr response;
    /// 错误描述
    std::string error;

    std::string toString() const;
};

class HttpConnectionPool;
/**
 * @brief HTTP客户端类
 */
class HttpConnection : public SocketStream {
friend class HttpConnectionPool;
public:
    /// HTTP客户端类智能指针
    typedef std::shared_ptr<HttpConnection> ptr;

    /**
     * @brief 发送HTTP的GET请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoGet(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP的GET请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoGet(Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoPost(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoPost(Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoRequest(HttpMethod method
                            , const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoRequest(HttpMethod method
                            , Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] req 请求结构体
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @return 返回HTTP结果结构体
     */
    static HttpResult::ptr DoRequest(HttpRequest::ptr req
                            , Uri::ptr uri
                            , uint64_t timeout_ms);

    /**
     * @brief 构造函数
     * @param[in] sock Socket类
     * @param[in] owner 是否掌握所有权
     */
    HttpConnection(Socket::ptr sock, bool owner = true);

    /**
     * @brief 析构函数
     */
    ~HttpConnection();

    /**
     * @brief chunked 流式回调策略
     * 满足以下任意条件时向应用层投递一次数据：
     *   1. 累积 chunk 数量达到 max_chunks（0 = 不限）
     *   2. 累积 body 字节数达到 max_buffer_size（0 = 不限）
     *   3. 所有 chunk 接收完毕（is_done = true，始终触发）
     */
    struct ChunkPolicy {
        size_t max_chunks      = 0;   ///< 每批最大 chunk 数，0 表示不限
        size_t max_buffer_size = 0;   ///< 每批最大字节数，0 表示不限
    };

    /**
     * @brief 流式 chunk 数据回调
     * @param data      本批次累积的 body 内容
     * @param is_done   是否已接收到最后一个 chunk
     */
    using ChunkCallback = std::function<void(const std::string& data, bool is_done)>;

    /**
     * @brief 接收HTTP响应
     */
    HttpResponse::ptr recvResponse();

    /**
     * @brief 流式接收 chunked 响应，按策略分批回调给应用层
     * @param[in] policy   触发回调的策略
     * @param[in] cb       每批数据回调，is_done=true 表示最后一批
     * @return 响应头部（body 已通过回调投递，response->getBody() 为空）
     *         失败返回 nullptr
     */
    HttpResponse::ptr recvResponseStream(const ChunkPolicy& policy,
                                         const ChunkCallback& cb);

    /**
     * @brief 发送HTTP的POST流式请求（建连→发送→流式回调）
     * @param[in] url          请求URL
     * @param[in] timeout_ms   超时（毫秒）
     * @param[in] headers      请求头
     * @param[in] body         请求体
     * @param[in] policy       chunk 投递策略
     * @param[in] cb           chunk 数据回调
     * @return HttpResult，其中 response->getBody() 为空（数据已通过 cb 投递）
     */
    static HttpResult::ptr DoPostStream(const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers
                            , const std::string& body
                            , const ChunkPolicy& policy
                            , const ChunkCallback& cb);

    /**
     * @brief 发送HTTP请求
     * @param[in] req HTTP请求结构
     */
    int sendRequest(HttpRequest::ptr req);

private:
    uint64_t m_createTime = 0;
    uint64_t m_request = 0;
};

class HttpConnectionPool {
public:
    typedef std::shared_ptr<HttpConnectionPool> ptr;
    typedef Mutex MutexType;

    static HttpConnectionPool::ptr Create(const std::string& uri
                                   ,const std::string& vhost
                                   ,uint32_t max_size
                                   ,uint32_t max_alive_time
                                   ,uint32_t max_request);

    HttpConnectionPool(const std::string& host
                       ,const std::string& vhost
                       ,uint32_t port
                       ,bool is_https
                       ,uint32_t max_size
                       ,uint32_t max_alive_time
                       ,uint32_t max_request);

    HttpConnection::ptr getConnection();


    /**
     * @brief 发送HTTP的GET请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doGet(const std::string& url
                          , uint64_t timeout_ms
                          , const std::map<std::string, std::string>& headers = {}
                          , const std::string& body = "");

    /**
     * @brief 发送HTTP的GET请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doGet(Uri::ptr uri
                           , uint64_t timeout_ms
                           , const std::map<std::string, std::string>& headers = {}
                           , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] url 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doPost(const std::string& url
                           , uint64_t timeout_ms
                           , const std::map<std::string, std::string>& headers = {}
                           , const std::string& body = "");

    /**
     * @brief 发送HTTP的POST请求
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doPost(Uri::ptr uri
                           , uint64_t timeout_ms
                           , const std::map<std::string, std::string>& headers = {}
                           , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri 请求的url
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doRequest(HttpMethod method
                            , const std::string& url
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] method 请求类型
     * @param[in] uri URI结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @param[in] headers HTTP请求头部参数
     * @param[in] body 请求消息体
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doRequest(HttpMethod method
                            , Uri::ptr uri
                            , uint64_t timeout_ms
                            , const std::map<std::string, std::string>& headers = {}
                            , const std::string& body = "");

    /**
     * @brief 发送HTTP请求
     * @param[in] req 请求结构体
     * @param[in] timeout_ms 超时时间(毫秒)
     * @return 返回HTTP结果结构体
     */
    HttpResult::ptr doRequest(HttpRequest::ptr req
                            , uint64_t timeout_ms);
private:
    static void ReleasePtr(HttpConnection* ptr, HttpConnectionPool* pool);
private:
    std::string m_host;
    std::string m_vhost;
    uint32_t m_port;
    uint32_t m_maxSize;
    uint32_t m_maxAliveTime;
    uint32_t m_maxRequest;
    bool m_isHttps;

    MutexType m_mutex;
    std::list<HttpConnection*> m_conns;
    std::atomic<int32_t> m_total = {0};
};

}
}

#endif