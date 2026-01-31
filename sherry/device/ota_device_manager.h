#ifndef __SHERRY_OTA_DEVICE_MANAGER_H__
#define __SHERRY_OTA_DEVICE_MANAGER_H__

#include <string>
#include <memory>
#include <vector>
#include <map>
#include "../sherry.h"
#include "ota_device_client.h"

namespace sherry {
namespace device {

/**
 * @brief 设备配置结构
 */
struct DeviceConfig {
    int device_no;                          // 设备编号
    uint16_t device_type;                   // 设备类型
    std::string mqtt_host;                  // MQTT服务器地址
    int mqtt_port;                          // MQTT服务器端口
    std::string mqtt_protocol;              // MQTT协议（tcp/ssl）
    std::string http_host;                  // HTTP服务器地址
    int http_port;                          // HTTP服务器端口
    std::vector<std::string> component_names;    // 组件名称列表
    std::vector<std::string> component_versions; // 组件版本列表
};

/**
 * @brief OTA设备管理器
 * 管理多个设备客户端，提供统一的配置和控制接口
 */
class OTADeviceManager {
public:
    typedef std::shared_ptr<OTADeviceManager> ptr;
    typedef RWMutex RWMutexType;

    OTADeviceManager();
    ~OTADeviceManager();

    /**
     * @brief 从配置创建并添加设备
     */
    bool add_device(const DeviceConfig& config);

    /**
     * @brief 移除设备
     */
    bool remove_device(int device_no);

    /**
     * @brief 获取设备
     */
    OTADeviceClient::ptr get_device(int device_no) const;

    /**
     * @brief 获取所有设备
     */
    std::vector<OTADeviceClient::ptr> get_all_devices() const;

    /**
     * @brief 设置全局升级回调
     */
    void set_upgrade_callback(OTADeviceClient::UpgradeCallback cb);

    /**
     * @brief 启动所有设备
     */
    void start_all();

    /**
     * @brief 停止所有设备
     */
    void stop_all();

    /**
     * @brief 从YAML配置文件加载设备
     */
    bool load_from_yaml(const std::string& yaml_file);

private:
    RWMutexType m_mutex;
    std::map<int, OTADeviceClient::ptr> m_devices;
    OTADeviceClient::UpgradeCallback m_global_upgrade_callback;
};

} // namespace device
} // namespace sherry

#endif
