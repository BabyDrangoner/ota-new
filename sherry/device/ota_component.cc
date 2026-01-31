#include "ota_component.h"
#include "../../include/json/json.hpp"
#include "../log.h"

namespace sherry {
namespace device {

OTAComponent::OTAComponent(const std::string& name, 
                           const std::string& version,
                           const std::string& upgrade_time)
    : m_name(name)
    , m_version(version)
    , m_upgrade_time(upgrade_time) {
    if (m_upgrade_time.empty()) {
        m_upgrade_time = sherry::getCurrentTimeString();
    }
}

void OTAComponent::set_version(const std::string& version) {
    Mutex::Lock lock(m_mutex);
    m_version = version;
}

void OTAComponent::set_upgrade_time(const std::string& time) {
    Mutex::Lock lock(m_mutex);
    m_upgrade_time = time;
}

std::string OTAComponent::to_json() const {
    Mutex::Lock lock(m_mutex);
    nlohmann::json j;
    j["name"] = m_name;
    j["version"] = m_version;
    j["upgrade_time"] = m_upgrade_time;
    return j.dump();
}

bool OTAComponent::need_upgrade(const std::string& remote_version) const {
    Mutex::Lock lock(m_mutex);
    // 简单的版本比较逻辑，可以根据需求扩展
    return remote_version > m_version;
}

} // namespace device
} // namespace sherry
