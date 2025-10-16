#ifndef _SHERRY_OTAMANAGER_H__
#define _SHERRY_OTAMANAGER_H__

#include "sherry.h"
#include "scheduler.h"
#include "ota_notifier.h"
#include "ota_query_responder.h"
#include "mqtt_client.h"
#include "timer.h"
#include "http/http.h"
#include "http/http_session.h"
#include "ota_client_callback.h"
#include "ota_http_response_builder.h"
#include "ota_subscribe_download.h"
#include "iomanager.h"

#include <unordered_map>
#include <unordered_set>

namespace sherry{
class HttpServer;
class OTAManager{
public:
    typedef std::shared_ptr<OTAManager> ptr;
    typedef RWMutex RWMutexType;
    OTAManager(size_t file_size, const std::string& protocol
                , const std::string& host, int port
                , const std::string& file_prev_path, IOManager::ptr io_mgr);

    static OTAManager* GetThis();
    void SetThis();

    void set_protocol(const std::string& v){ m_protocol = v;}
    void set_host(const std::string& v){ m_host = v;}
    void set_port(int v) { m_port = v;}
    
    std::string get_protocol() const{ return m_protocol;}
    std::string get_host() const { return m_host;}
    int get_port() const { return m_port;}
    uint16_t get_device_types_counts() const { return m_device_types_counts;}
    uint64_t get_device_counts() const { return m_device_counts;}

    bool add_device(uint16_t device_type, uint32_t device_no);
    bool remove_device(uint16_t device_type, uint32_t device_no);

    void ota_notify(uint64_t device_type
                    , const std::string& name
                    , const std::string& version
                    , http::HttpResponse::ptr rsp);
    void ota_stop_notify(uint16_t device_type
                        , const std::string& name
                        , const std::string& version
                        , http::HttpResponse::ptr rsp);
    void ota_query(uint16_t device_type
                  , uint32_t device_no
                  , const std::string& action
                  , http::HttpResponse::ptr rsp);
    void ota_query_download(uint16_t device_type
                           , uint32_t device_no
                           , const std::string& detail
                           , http::HttpResponse::ptr rsp);
    void ota_file_download(uint16_t device_type
                           , const std::string& name
                           , const std::string& version
                           , http::HttpResponse::ptr rsp
                           , http::HttpSession::ptr session);

    int getFileDetail(const std::string& file_path, struct FileDetail& file_detail);


    bool check_device(uint16_t device_type, uint32_t device_no);
    bool check_device(uint16_t device_type);

    static int send_file(int fd, off_t* offset, size_t file_size, http::HttpSession::ptr session);
    static void sendFile(int fd, Fiber::ptr thisFiber, off_t offset, size_t file_size, http::HttpSession::ptr session);

private:
    bool get_notify_message(uint16_t device_type, const std::string& name, const std::string& version, struct OTAMessage& msg);

private:
    RWMutexType m_mutex;
    RWMutexType m_notifier_mutex;
    std::string m_protocol;
    std::string m_host;
    int m_port;

    std::string m_file_prev_path;
    ssize_t m_buffer_size;

    IOManager::ptr m_timer_mgr;
    uint16_t m_device_types_counts;
    uint64_t m_device_counts;
    OTAClientCallbackManager::ptr m_callback_mgr;
    MqttClientManager::ptr m_client_mgr;
    
    std::unordered_map<uint16_t, std::unordered_set<uint32_t>> m_device_type_nums;
    std::unordered_map<std::string, OTANotifier::ptr> m_ota_notifier_map;
    std::unordered_map<uint16_t, std::unordered_map<uint32_t, OTASubscribeDownload::ptr>> m_ota_subscribe_download_map;
    
};
}
#endif