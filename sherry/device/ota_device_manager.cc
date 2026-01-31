#include "ota_device_manager.h"
#include "../log.h"
#include "../config.h"
#include <yaml-cpp/yaml.h>

namespace sherry {
namespace device {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

OTADeviceManager::OTADeviceManager() {
}

OTADeviceManager::~OTADeviceManager() {
    stop_all();
}

bool OTADeviceManager::add_device(const DeviceConfig& config) {
    RWMutex::WriteLock lock(m_mutex);
    
    // 检查设备是否已存在
    if (m_devices.find(config.device_no) != m_devices.end()) {
        SYLAR_LOG_WARN(g_logger) << "Device already exists: " << config.device_no;
        return false;
    }
    
    // 创建设备客户端
    auto device = std::make_shared<OTADeviceClient>(config.device_no, config.device_type);
    
    // 添加组件
    if (config.component_names.size() != config.component_versions.size()) {
        SYLAR_LOG_ERROR(g_logger) << "Component names and versions size mismatch";
        return false;
    }
    
    for (size_t i = 0; i < config.component_names.size(); ++i) {
        auto component = std::make_shared<OTAComponent>(
            config.component_names[i], 
            config.component_versions[i]
        );
        device->add_component(component);
    }
    
    // 设置服务器地址
    device->set_http_server(config.http_host, config.http_port);
    
    // 设置升级回调
    if (m_global_upgrade_callback) {
        device->set_upgrade_callback(m_global_upgrade_callback);
    }
    
    // 连接MQTT服务器
    if (!device->connect(config.mqtt_host, config.mqtt_port, config.mqtt_protocol)) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to connect device: " << config.device_no;
        return false;
    }
    
    m_devices[config.device_no] = device;
    
    SYLAR_LOG_INFO(g_logger) << "Device added successfully: " << config.device_no;
    return true;
}

bool OTADeviceManager::remove_device(int device_no) {
    RWMutex::WriteLock lock(m_mutex);
    
    auto it = m_devices.find(device_no);
    if (it == m_devices.end()) {
        SYLAR_LOG_WARN(g_logger) << "Device not found: " << device_no;
        return false;
    }
    
    it->second->stop();
    m_devices.erase(it);
    
    SYLAR_LOG_INFO(g_logger) << "Device removed: " << device_no;
    return true;
}

OTADeviceClient::ptr OTADeviceManager::get_device(int device_no) const {
    RWMutex::ReadLock lock(const_cast<RWMutexType&>(m_mutex));
    
    auto it = m_devices.find(device_no);
    if (it != m_devices.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<OTADeviceClient::ptr> OTADeviceManager::get_all_devices() const {
    RWMutex::ReadLock lock(const_cast<RWMutex&>(m_mutex));
    
    std::vector<OTADeviceClient::ptr> result;
    for (auto& pair : m_devices) {
        result.push_back(pair.second);
    }
    return result;
}

void OTADeviceManager::set_upgrade_callback(OTADeviceClient::UpgradeCallback cb) {
    RWMutex::WriteLock lock(m_mutex);
    m_global_upgrade_callback = cb;
    
    // 更新所有已存在设备的回调
    for (auto& pair : m_devices) {
        pair.second->set_upgrade_callback(cb);
    }
}

void OTADeviceManager::start_all() {
    RWMutex::ReadLock lock(m_mutex);
    
    for (auto& pair : m_devices) {
        pair.second->start();
    }
    
    SYLAR_LOG_INFO(g_logger) << "All devices started, count: " << m_devices.size();
}

void OTADeviceManager::stop_all() {
    RWMutex::ReadLock lock(m_mutex);
    
    for (auto& pair : m_devices) {
        pair.second->stop();
    }
    
    SYLAR_LOG_INFO(g_logger) << "All devices stopped";
}

bool OTADeviceManager::load_from_yaml(const std::string& yaml_file) {
    try {
        YAML::Node config = YAML::LoadFile(yaml_file);
        
        if (!config["devices"]) {
            SYLAR_LOG_ERROR(g_logger) << "No 'devices' section in config file";
            return false;
        }
        
        auto devices_node = config["devices"];
        for (size_t i = 0; i < devices_node.size(); ++i) {
            auto dev_node = devices_node[i];
            
            DeviceConfig dev_config;
            dev_config.device_no = dev_node["device_no"].as<int>();
            dev_config.device_type = dev_node["device_type"].as<uint16_t>();
            dev_config.mqtt_host = dev_node["mqtt_host"].as<std::string>();
            dev_config.mqtt_port = dev_node["mqtt_port"].as<int>();
            dev_config.mqtt_protocol = dev_node["mqtt_protocol"].as<std::string>("tcp");
            dev_config.http_host = dev_node["http_host"].as<std::string>();
            dev_config.http_port = dev_node["http_port"].as<int>();
            
            // 加载组件列表
            if (dev_node["components"]) {
                auto comps_node = dev_node["components"];
                for (size_t j = 0; j < comps_node.size(); ++j) {
                    auto comp_node = comps_node[j];
                    dev_config.component_names.push_back(comp_node["name"].as<std::string>());
                    dev_config.component_versions.push_back(comp_node["version"].as<std::string>());
                }
            }
            
            if (!add_device(dev_config)) {
                SYLAR_LOG_ERROR(g_logger) << "Failed to add device from config: " 
                                          << dev_config.device_no;
            }
        }
        
        SYLAR_LOG_INFO(g_logger) << "Loaded " << m_devices.size() 
                                 << " devices from config file";
        return true;
        
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to load devices from YAML: " << e.what();
        return false;
    }
}

} // namespace device
} // namespace sherry
