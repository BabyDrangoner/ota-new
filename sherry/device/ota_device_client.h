#ifndef __SHERRY_OTA_DEVICE_CLIENT_H__
#define __SHERRY_OTA_DEVICE_CLIENT_H__

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include "../sherry.h"
#include "../mqtt_client.h"
#include "../http/http_connection.h"
#include "../iomanager.h"
#include "ota_component.h"

namespace sherry {
namespace device {

/**
 * @brief OTA设备客户端
 * 代表一个设备终端，负责与服务端通信，处理OTA升级
 */
class OTADeviceClient : public std::enable_shared_from_this<OTADeviceClient> {
public:
    typedef std::shared_ptr<OTADeviceClient> ptr;
    typedef RWMutex RWMutexType;

    /**
     * @brief 升级回调函数类型
     * @param component_name 组件名称
     * @param old_version 旧版本
     * @param new_version 新版本
     * @param file_path 下载的文件路径
     * @return true表示升级成功，false表示失败
     */
    typedef std::function<bool(const std::string& component_name,
                              const std::string& old_version,
                              const std::string& new_version,
                              const std::string& file_path)> UpgradeCallback;

    /**
     * @brief 构造函数
     * @param device_no 设备编号
     * @param device_type 设备类型
     */
    OTADeviceClient(int device_no, uint16_t device_type = 1);

    // 基本信息
    int get_device_no() const { return m_device_no; }
    uint16_t get_device_type() const { return m_device_type; }

    /**
     * @brief 添加组件
     */
    void add_component(OTAComponent::ptr component);

    /**
     * @brief 获取所有组件
     */
    std::vector<OTAComponent::ptr> get_components() const;

    /**
     * @brief 根据名称获取组件
     */
    OTAComponent::ptr get_component(const std::string& name) const;

    /**
     * @brief 设置升级回调
     */
    void set_upgrade_callback(UpgradeCallback cb);

    /**
     * @brief 连接到MQTT服务器
     */
    bool connect(const std::string& host, int port, const std::string& protocol = "tcp");

    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 启动设备客户端（开始监听服务端消息）
     */
    void start();

    /**
     * @brief 停止设备客户端
     */
    void stop();

    /**
     * @brief 检查是否正在运行
     */
    bool is_running() const { return m_running; }

    /**
     * @brief 设置下载目录
     */
    void set_download_dir(const std::string& dir) { m_download_dir = dir; }

    /**
     * @brief 设置HTTP服务器地址（用于文件下载）
     */
    void set_http_server(const std::string& host, int port);

private:
    /**
     * @brief MQTT消息到达回调
     */
    static void on_message_arrived(void* context, char* topic_name, 
                                   int topic_len, MQTTAsync_message* message);

    /**
     * @brief 处理查询请求
     */
    void handle_query_request(const std::string& payload);

    /**
     * @brief 处理查询下载请求（notify消息）
     */
    void handle_query_download_request(const std::string& payload);

    /**
     * @brief 响应查询请求，上报组件信息
     */
    void response_query();

    /**
     * @brief 执行OTA升级流程
     * @param name 组件名称
     * @param version 新版本号
     * @param url_path 下载路径
     * @param md5_value MD5校验值
     * @param file_name 文件名
     */
    void perform_upgrade(const std::string& name,
                        const std::string& version,
                        const std::string& url_path,
                        const std::string& md5_value,
                        const std::string& file_name);

    /**
     * @brief 从HTTP服务器下载文件
     * @param url_path URL路径
     * @param save_path 保存路径
     * @return true表示下载成功
     */
    bool download_file(const std::string& url_path, const std::string& save_path);

    /**
     * @brief 验证文件MD5
     */
    bool verify_md5(const std::string& file_path, const std::string& expected_md5);

    /**
     * @brief 订阅主题
     */
    void subscribe_topics();

    /**
     * @brief 生成上报消息的主题
     */
    std::string get_response_topic() const;

private:
    int m_device_no;                          // 设备编号
    uint16_t m_device_type;                   // 设备类型
    bool m_running;                           // 是否运行中

    mutable RWMutexType m_component_mutex;
    std::map<std::string, OTAComponent::ptr> m_components;  // 组件列表

    MqttClient::ptr m_mqtt_client;            // MQTT客户端
    std::string m_mqtt_host;
    int m_mqtt_port;
    std::string m_mqtt_protocol;

    std::string m_http_host;                  // HTTP服务器地址
    int m_http_port;
    std::string m_download_dir;               // 下载目录

    UpgradeCallback m_upgrade_callback;       // 升级回调
    Mutex m_callback_mutex;

    IOManager::ptr m_iomanager;               // IO管理器
};

} // namespace device
} // namespace sherry

#endif
