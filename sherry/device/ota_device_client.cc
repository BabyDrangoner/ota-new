#include "ota_device_client.h"
#include "../../include/json/json.hpp"
#include "../log.h"
#include "../util.h"
#include "../hash.h"
#include "../http/http_connection.h"
#include "../http/http.h"
#include "../uri.h"
#include <fstream>
#include <sys/stat.h>
#include <openssl/evp.h>

namespace sherry {
namespace device {

// 辅助函数：计算文件的MD5值（使用EVP API兼容OpenSSL 3.0）
static std::string calculate_file_md5(const char* filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return "";
    }
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(ctx);
            return "";
        }
    }
    
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    
    EVP_MD_CTX_free(ctx);
    
    // 转换为十六进制字符串
    char md5string[33];
    for (unsigned int i = 0; i < digest_len; ++i) {
        sprintf(&md5string[i*2], "%02x", (unsigned int)digest[i]);
    }
    md5string[32] = '\0';
    
    return std::string(md5string);
}

static sherry::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

OTADeviceClient::OTADeviceClient(int device_no, uint16_t device_type)
    : m_device_no(device_no)
    , m_device_type(device_type)
    , m_running(false)
    , m_mqtt_port(1883)
    , m_mqtt_protocol("tcp")
    , m_http_port(8020)
    , m_download_dir("./downloads/") {
    
    m_iomanager = std::make_shared<IOManager>(2, false, "device_client");
    
    // 确保下载目录存在
    mkdir(m_download_dir.c_str(), 0755);
}

void OTADeviceClient::add_component(OTAComponent::ptr component) {
    RWMutex::WriteLock lock(m_component_mutex);
    m_components[component->get_name()] = component;
}

std::vector<OTAComponent::ptr> OTADeviceClient::get_components() const {
    RWMutex::ReadLock lock(m_component_mutex);
    std::vector<OTAComponent::ptr> result;
    for (auto& pair : m_components) {
        result.push_back(pair.second);
    }
    return result;
}

OTAComponent::ptr OTADeviceClient::get_component(const std::string& name) const {
    RWMutex::ReadLock lock(m_component_mutex);
    auto it = m_components.find(name);
    if (it != m_components.end()) {
        return it->second;
    }
    return nullptr;
}

void OTADeviceClient::set_upgrade_callback(UpgradeCallback cb) {
    Mutex::Lock lock(m_callback_mutex);
    m_upgrade_callback = cb;
}

void OTADeviceClient::set_http_server(const std::string& host, int port) {
    m_http_host = host;
    m_http_port = port;
}

bool OTADeviceClient::connect(const std::string& host, int port, const std::string& protocol) {
    m_mqtt_host = host;
    m_mqtt_port = port;
    m_mqtt_protocol = protocol;

    std::string client_id = "device_" + std::to_string(m_device_type) 
                          + "_" + std::to_string(m_device_no);
    
    m_mqtt_client = std::make_shared<MqttClient>(protocol, port, host, client_id, nullptr);
    
    // 连接到MQTT服务器
    m_mqtt_client->connect();    
    // 等待连接成功
    sleep(1);
    
    bool connected = m_mqtt_client->get_isconnected();    if (connected) {
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] connected to MQTT broker: " 
                                 << host << ":" << port;
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] failed to connect to MQTT broker";
    }
    
    return connected;
}

void OTADeviceClient::disconnect() {
    if (m_mqtt_client) {
        m_mqtt_client->disconnect();
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no << "] disconnected";
    }
}

void OTADeviceClient::start() {
    if (m_running) {
        SYLAR_LOG_WARN(g_logger) << "Device [" << m_device_no << "] already running";
        return;
    }

    if (!m_mqtt_client || !m_mqtt_client->get_isconnected()) {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] not connected to MQTT broker";
        return;
    }

    m_running = true;
    
    // 订阅相关主题
    subscribe_topics();
    
    SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no << "] started";
}

void OTADeviceClient::stop() {
    m_running = false;
    disconnect();
    SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no << "] stopped";
}

void OTADeviceClient::subscribe_topics() {
    // 订阅查询主题：/ota/device/query/{device_type}/gps
    std::string query_topic = "/ota/device/query/" 
                            + std::to_string(m_device_type) + "/gps";
    m_mqtt_client->subscribe(query_topic, 1);
    SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                             << "] subscribed to: " << query_topic;

    // 订阅升级通知主题：/ota/device/query_download/{device_type}/gps
    std::string notify_topic = "/ota/device/query_download/" 
                             + std::to_string(m_device_type) + "/gps";
    m_mqtt_client->subscribe(notify_topic, 1);
    SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                             << "] subscribed to: " << notify_topic;

    // 订阅每个组件的版本发布通知：/ota/{device_type}/{component_name}/notify
    auto components = get_components();
    for (auto& comp : components) {
        std::string component_notify_topic = "/ota/" 
                                           + std::to_string(m_device_type) 
                                           + "/" + comp->get_name() 
                                           + "/notify";
        m_mqtt_client->subscribe(component_notify_topic, 1);
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] subscribed to component notify: " 
                                 << component_notify_topic;
    }
}

std::string OTADeviceClient::get_response_topic() const {
    // 响应主题：/ota/server/query/{device_type}/gps
    return "/ota/server/query/" + std::to_string(m_device_type) + "/gps";
}

void OTADeviceClient::on_message_arrived(void* context, char* topic_name, 
                                        int topic_len, MQTTAsync_message* message) {
    OTADeviceClient* client = static_cast<OTADeviceClient*>(context);
    if (!client || !client->m_running) {
        MQTTAsync_freeMessage(&message);
        MQTTAsync_free(topic_name);
        return;
    }

    std::string topic(topic_name);
    std::string payload((char*)message->payload, message->payloadlen);
    
    SYLAR_LOG_DEBUG(g_logger) << "Device [" << client->m_device_no 
                              << "] received message on topic: " << topic
                              << ", payload: " << payload;

    // 根据主题分发处理
    if (topic.find("/ota/device/query/") != std::string::npos) {
        // 查询请求
        client->m_iomanager->schedule([client, payload]() {
            client->handle_query_request(payload);
        });
    } else if (topic.find("/ota/device/query_download/") != std::string::npos) {
        // 升级通知
        client->m_iomanager->schedule([client, payload]() {
            client->handle_query_download_request(payload);
        });
    } else if (topic.find("/notify") != std::string::npos) {
        // 服务端版本发布通知：/ota/{device_type}/{component_name}/notify
        client->m_iomanager->schedule([client, payload]() {
            SYLAR_LOG_INFO(g_logger) << "Device [" << client->m_device_no 
                                     << "] received version notify: " << payload;
            // 处理版本发布通知（与query_download相同）
            client->handle_query_download_request(payload);
        });
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topic_name);
}

void OTADeviceClient::handle_query_request(const std::string& payload) {
    SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                             << "] handling query request";
    
    // 响应查询，上报当前组件版本信息
    response_query();
}

void OTADeviceClient::response_query() {
    try {
        nlohmann::json response;
        response["action"] = "query";
        response["no"] = m_device_no;
        response["time"] = sherry::getCurrentTimeString();
        
        nlohmann::json results = nlohmann::json::array();
        
        auto components = get_components();
        for (auto& comp : components) {
            nlohmann::json comp_json;
            comp_json["name"] = comp->get_name();
            comp_json["version"] = comp->get_version();
            comp_json["upgrade_time"] = comp->get_upgrade_time();
            results.push_back(comp_json);
        }
        
        response["results"] = results;
        
        std::string response_str = response.dump();
        std::string topic = get_response_topic();
        
        m_mqtt_client->publish(topic, response_str, 1);
        
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] sent query response: " << response_str;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] failed to send query response: " << e.what();
    }
}

void OTADeviceClient::handle_query_download_request(const std::string& payload) {
    try {
        nlohmann::json notify = nlohmann::json::parse(payload);
        
        std::string name = notify["name"];
        std::string version = notify["version"];
        std::string url_path = notify["url_path"];
        std::string md5_value = notify["md5_value"];
        std::string file_name = notify["file_name"];
        
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] received upgrade notify for component: " << name
                                 << ", version: " << version;
        
        // 检查是否需要升级
        auto component = get_component(name);
        if (!component) {
            SYLAR_LOG_WARN(g_logger) << "Device [" << m_device_no 
                                     << "] component not found: " << name;
            return;
        }
        
        if (!component->need_upgrade(version)) {
            SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                     << "] component " << name 
                                     << " already up-to-date";
            return;
        }
        
        // 执行升级
        perform_upgrade(name, version, url_path, md5_value, file_name);
        
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] failed to handle upgrade notify: " << e.what();
    }
}

void OTADeviceClient::perform_upgrade(const std::string& name,
                                      const std::string& version,
                                      const std::string& url_path,
                                      const std::string& md5_value,
                                      const std::string& file_name) {
    SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                             << "] starting upgrade for component: " << name;
    
    // 1. 下载文件
    std::string save_path = m_download_dir + file_name;
    if (!download_file(url_path, save_path)) {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] failed to download file: " << url_path;
        return;
    }
    
    // 2. 验证MD5
    if (!verify_md5(save_path, md5_value)) {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] MD5 verification failed for file: " << save_path;
        return;
    }
    
    // 3. 执行升级回调
    auto component = get_component(name);
    std::string old_version = component->get_version();
    
    bool success = false;
    {
        Mutex::Lock lock(m_callback_mutex);
        if (m_upgrade_callback) {
            success = m_upgrade_callback(name, old_version, version, save_path);
        } else {
            // 默认升级逻辑：仅更新版本号
            success = true;
        }
    }
    
    if (success) {
        // 4. 更新组件版本信息
        component->set_version(version);
        component->set_upgrade_time(sherry::getCurrentTimeString());
        
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] successfully upgraded component: " << name
                                 << " from " << old_version << " to " << version;
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                  << "] upgrade callback failed for component: " << name;
    }
}

bool OTADeviceClient::download_file(const std::string& url_path, 
                                    const std::string& save_path) {
    try {
        // 构建完整URL
        std::string url = "http://" + m_http_host + ":" 
                        + std::to_string(m_http_port) + url_path;
        
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] downloading file from: " << url;
        
        // 创建HTTP连接
        auto uri = Uri::Create(url);
        if (!uri) {
            SYLAR_LOG_ERROR(g_logger) << "Invalid URL: " << url;
            return false;
        }
        
        auto result = http::HttpConnection::DoGet(uri, 10000);
        if (!result || result->result != (int)http::HttpResult::Error::OK) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to download file: " 
                                      << (result ? (int)result->result : -1);
            return false;
        }
        
        auto response = result->response;
        if (!response || response->getStatus() != http::HttpStatus::OK) {
            SYLAR_LOG_ERROR(g_logger) << "HTTP error: " 
                                      << (response ? (int)response->getStatus() : -1);
            return false;
        }
        
        // 保存文件
        std::ofstream ofs(save_path, std::ios::binary);
        if (!ofs) {
            SYLAR_LOG_ERROR(g_logger) << "Failed to open file for writing: " << save_path;
            return false;
        }
        
        ofs << response->getBody();
        ofs.close();
        
        SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                 << "] file downloaded successfully: " << save_path;
        
        return true;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Download exception: " << e.what();
        return false;
    }
}

bool OTADeviceClient::verify_md5(const std::string& file_path, 
                                 const std::string& expected_md5) {
    try {
        std::string actual_md5 = calculate_file_md5(file_path.c_str());
        
        if (actual_md5 == expected_md5) {
            SYLAR_LOG_INFO(g_logger) << "Device [" << m_device_no 
                                     << "] MD5 verification passed for: " << file_path;
            return true;
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Device [" << m_device_no 
                                      << "] MD5 mismatch - expected: " << expected_md5
                                      << ", actual: " << actual_md5;
            return false;
        }
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "MD5 verification exception: " << e.what();
        return false;
    }
}

} // namespace device
} // namespace sherry
