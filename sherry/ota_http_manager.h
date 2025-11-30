#ifndef __SHERRY_OTA_HTTP_MANAGER_H__
#define __SHERRY_OTA_HTTP_MANAGER_H__

#include <string>
#include <memory>
#include "http/http_server.h"
#include "http/http.h"
#include "http/http_session.h"
#include "ota_notifier.h"

namespace sherry {

/**
 * @brief OTA HTTP 管理器
 * 封装 OTA 相关的 HTTP 路由处理逻辑
 */
class OTAHttpManager {
public:
    typedef std::shared_ptr<OTAHttpManager> ptr;

    OTAHttpManager(const std::string& redis_http_pool_name
                   , const std::string& file_prev_path
                   , const std::string& ota_html_content = "");

    void register_routes(http::HttpServer::ptr server);
    void set_ota_html(const std::string& html);

private:
    int handle_notify(http::HttpRequest::ptr req,
                      http::HttpResponse::ptr rsp,
                      http::HttpSession::ptr session);

    int handle_stop_notify(http::HttpRequest::ptr req,
                           http::HttpResponse::ptr rsp,
                           http::HttpSession::ptr session);

    int handle_query(http::HttpRequest::ptr req,
                     http::HttpResponse::ptr rsp,
                     http::HttpSession::ptr session);

    int handle_query_download(http::HttpRequest::ptr req,
                              http::HttpResponse::ptr rsp,
                              http::HttpSession::ptr session);

    int handle_file_download(http::HttpRequest::ptr req,
                             http::HttpResponse::ptr rsp,
                             http::HttpSession::ptr session);

    int handle_ota_page(http::HttpRequest::ptr req,
                       http::HttpResponse::ptr rsp,
                       http::HttpSession::ptr session);
    int ota_notify(uint16_t device_type
                   , const std::string& name
                   , const std::string& version
                   , const std::string& command
                   , http::HttpResponse::ptr rsp);
    
    int ota_stop_notify(uint16_t device_type
                        , const std::string& name
                        , const std::string& version
                        , const std::string& command
                        , http::HttpResponse::ptr rsp);
    void ota_file_download(uint16_t device_type
                           , const std::string& name
                           , const std::string& version
                           , http::HttpResponse::ptr rsp
                           , http::HttpSession::ptr session);
    int send_file(int fd, off_t* offset, size_t file_size
                  , http::HttpSession::ptr session);
    void sendFile(int fd, Fiber::ptr thisFiber, off_t offset
                  , size_t file_size, http::HttpSession::ptr session);
    int getFileDetail(const std::string& file_path, struct FileDetail& file_detail);

    void set_options(http::HttpResponse::ptr rsp);
    void set_server_error(http::HttpResponse::ptr rsp);

private:
    std::string m_redis_pool_name;
    std::string m_file_prev_path;
    std::string m_ota_html_content;
};

}

#endif
