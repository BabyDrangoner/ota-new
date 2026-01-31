#include "../sherry/device/ota_device_manager.h"
#include "../sherry/device/ota_device_client.h"
#include "../sherry/device/ota_component.h"
#include "../sherry/log.h"
#include "../sherry/config.h"
#include <signal.h>
#include <unistd.h>

using namespace sherry;
using namespace sherry::device;

static Logger::ptr g_logger = SYLAR_LOG_ROOT();
static OTADeviceManager::ptr device_manager;

// 信号处理
void signal_handler(int sig) {
    SYLAR_LOG_INFO(g_logger) << "Received signal: " << sig << ", stopping devices...";
    if (device_manager) {
        device_manager->stop_all();
    }
    exit(0);
}

// 自定义升级回调示例
bool custom_upgrade_callback(const std::string& component_name,
                            const std::string& old_version,
                            const std::string& new_version,
                            const std::string& file_path) {
    SYLAR_LOG_INFO(g_logger) << "=== Custom Upgrade Callback ===" 
                             << "\n  Component: " << component_name
                             << "\n  Old Version: " << old_version
                             << "\n  New Version: " << new_version
                             << "\n  File Path: " << file_path;
    
    // 这里可以实现具体的升级逻辑，例如：
    // 1. 停止相关服务
    // 2. 备份旧文件
    // 3. 安装新文件
    // 4. 重启服务
    // 5. 验证升级结果
    
    // 模拟升级过程
    SYLAR_LOG_INFO(g_logger) << "Simulating upgrade process...";
    sleep(1);
    
    // 返回true表示升级成功
    SYLAR_LOG_INFO(g_logger) << "Upgrade completed successfully!";
    return true;
}

// 监控设备状态
void monitor_devices(OTADeviceManager::ptr manager) {
    while (true) {
        sleep(10);
        
        auto devices = manager->get_all_devices();
        SYLAR_LOG_INFO(g_logger) << "=== Device Status Monitor ===" 
                                 << "\n  Total Devices: " << devices.size();
        
        for (auto& device : devices) {
            auto components = device->get_components();
            SYLAR_LOG_INFO(g_logger) << "  Device [" << device->get_device_no() 
                                     << "] Type: " << device->get_device_type()
                                     << ", Running: " << device->is_running()
                                     << ", Components: " << components.size();
            
            for (auto& comp : components) {
                SYLAR_LOG_INFO(g_logger) << "    - " << comp->get_name() 
                                         << " v" << comp->get_version()
                                         << " (upgraded: " << comp->get_upgrade_time() << ")";
            }
        }
    }
}

int main(int argc, char** argv) {
    // 设置日志级别
    g_logger->setLevel(LogLevel::INFO);
    
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "  OTA Device Client Starting...";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建设备管理器
    device_manager = std::make_shared<OTADeviceManager>();
    
    // 设置全局升级回调
    device_manager->set_upgrade_callback(custom_upgrade_callback);
    
    // 从配置文件加载设备
    std::string config_file = "./config/ota_device.yaml";
    if (argc > 1) {
        config_file = argv[1];
    }
    
    SYLAR_LOG_INFO(g_logger) << "Loading devices from: " << config_file;
    
    if (!device_manager->load_from_yaml(config_file)) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to load devices from config file";
        return -1;
    }
    
    // 启动所有设备
    SYLAR_LOG_INFO(g_logger) << "Starting all devices...";
    device_manager->start_all();
    
    SYLAR_LOG_INFO(g_logger) << "All devices started successfully!";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    SYLAR_LOG_INFO(g_logger) << "Device client is running...";
    SYLAR_LOG_INFO(g_logger) << "Press Ctrl+C to stop";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    
    // 启动监控线程
    std::thread monitor_thread(monitor_devices, device_manager);
    monitor_thread.detach();
    
    // 保持主线程运行
    while (true) {
        sleep(1);
    }
    
    return 0;
}
