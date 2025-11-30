#include "ota_command_dispatcher.h"
#include "ota_mqtt_manager.h"
#include "log.h"

#define TAG "OTACommandDispatcher"

namespace sherry {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

OTACommandDispatcher::OTACommandDispatcher(OTAMqttManager* ota_mgr)
    : m_ota_mgr(ota_mgr) {
    
    // 初始化命令处理映射表
    m_command_handlers["notify"] = [this](const nlohmann::json& cmd) {
        this->handle_notify(cmd);
    };
    
    m_command_handlers["stop_notify"] = [this](const nlohmann::json& cmd) {
        this->handle_stop_notify(cmd);
    };
    
    SYLAR_LOG_INFO(g_logger) << TAG << " initialized with " 
        << m_command_handlers.size() << " command handlers";
}

OTACommandDispatcher::CommandCallback OTACommandDispatcher::get_cb_by_command(const std::string& command) {
    auto it = m_command_handlers.find(command);
    if (it == m_command_handlers.end()) {
        SYLAR_LOG_WARN(g_logger) << TAG << " unknown command: " << command;
        return []() {
            SYLAR_LOG_ERROR(g_logger) << TAG << " empty callback executed";
        };
    }
    
    // 返回一个无参数的回调，内部调用实际的处理函数
    return [this, command]() {
        try {
            auto handler_it = m_command_handlers.find(command);
            if (handler_it != m_command_handlers.end()) {
                // 这里需要从某处获取完整的 JSON，暂时留空
                // 实际使用时应该通过 dispatch 方法
                SYLAR_LOG_WARN(g_logger) << TAG 
                    << " get_cb_by_command should not be used directly, use dispatch instead";
            }
        } catch (const std::exception& e) {
            SYLAR_LOG_ERROR(g_logger) << TAG 
                << " exception in command callback: " << e.what();
        }
    };
}

void OTACommandDispatcher::dispatch(const std::string& command_json) {
    try {
        nlohmann::json cmd = nlohmann::json::parse(command_json);
        std::string command_type = cmd["command"];
        
        auto it = m_command_handlers.find(command_type);
        if (it == m_command_handlers.end()) {
            SYLAR_LOG_WARN(g_logger) << TAG 
                << " unknown command type: " << command_type;
            return;
        }
        
        SYLAR_LOG_INFO(g_logger) << TAG 
            << " dispatching command: " << command_type;
        
        // 调用对应的处理函数
        it->second(cmd);
        
    } catch (const nlohmann::json::parse_error& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG 
            << " JSON parse error: " << e.what() 
            << ", input: " << command_json;
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG 
            << " exception in dispatch: " << e.what();
    }
}

void OTACommandDispatcher::handle_notify(const nlohmann::json& command_json) {
    try {        
        uint16_t device_type = command_json["device_type"];
        std::string name = command_json["name"];
        std::string version = command_json["version"];
        m_ota_mgr->ota_notify(device_type, name, version);
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG 
            << " exception in handle_notify: " << e.what();
    }
}

void OTACommandDispatcher::handle_stop_notify(const nlohmann::json& command_json) {
    try {
        uint16_t device_type = command_json["device_type"];
        std::string name = command_json["name"];
        std::string version = command_json["version"];
        
        SYLAR_LOG_INFO(g_logger) << TAG 
            << " handling stop_notify: device_type=" << device_type
            << ", name=" << name 
            << ", version=" << version;
        
        m_ota_mgr->ota_stop_notify(device_type, name, version);
        
    } catch (const std::exception& e) {
        SYLAR_LOG_ERROR(g_logger) << TAG 
            << " exception in handle_stop_notify: " << e.what();
    }
}

}
