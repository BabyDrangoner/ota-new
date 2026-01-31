#include "device_component.h"
#include "sherry/log.h"

namespace sherry{
namespace device{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void Component::update(const std::string& version
                       ,const std::string& md5_value
                       ,const std::string& upgrade_time
                       ,const std::string& http_uri){
    RWMutex::WriteLock lock(m_mutex);
    m_info.version = version;
    m_info.md5_value = md5_value;
    m_info.upgrade_time = upgrade_time;
    m_info.http_uri = http_uri;
}

const *ComponentInfo Component::get_info(){
    RWMutex::ReadLock lock(m_mutex);
    return &m_info;
}

} // namespace device
} // namespace sherry