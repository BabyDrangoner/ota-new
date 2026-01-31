#ifndef __SHERRY_OTA_COMPONENT_H__
#define __SHERRY_OTA_COMPONENT_H__

#include <string>
#include <memory>
#include "../sherry.h"

namespace sherry {
namespace device {

/**
 * @brief OTA组件类
 * 表示设备上的一个软件组件，包含名称、当前版本、升级时间等信息
 */
class OTAComponent {
public:
    typedef std::shared_ptr<OTAComponent> ptr;

    /**
     * @brief 构造函数
     * @param name 组件名称（如"gps", "mcu", "app"等）
     * @param version 当前版本号（如"1.0.0"）
     * @param upgrade_time 上次升级时间（如"2024-01-01 10:00:00"）
     */
    OTAComponent(const std::string& name, 
                 const std::string& version,
                 const std::string& upgrade_time = "");

    // Getters
    std::string get_name() const { return m_name; }
    std::string get_version() const { return m_version; }
    std::string get_upgrade_time() const { return m_upgrade_time; }

    // Setters
    void set_version(const std::string& version);
    void set_upgrade_time(const std::string& time);

    /**
     * @brief 将组件信息转换为JSON字符串
     */
    std::string to_json() const;

    /**
     * @brief 比较版本号
     * @return true表示需要升级（远程版本高于当前版本）
     */
    bool need_upgrade(const std::string& remote_version) const;

private:
    std::string m_name;           // 组件名称
    std::string m_version;        // 当前版本
    std::string m_upgrade_time;   // 升级时间
    mutable Mutex m_mutex;        // 保护组件数据
};

} // namespace device
} // namespace sherry

#endif
