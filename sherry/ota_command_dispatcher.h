#ifndef __SHERRY_OTA_COMMAND_DISPATCHER_
#define __SHERRY_OTA_COMMAND_DISPATCHER_

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include "../include/json/json.hpp"

namespace sherry{

class OTAMqttManager;

/**
 * @brief OTA 命令分发器
 * 根据从 Redis 消息队列接收到的命令 JSON，解析并调用对应的 OTAManager 函数
 * 
 * 支持的命令类型：
 * - notify: 发起 OTA 通知
 * - stop_notify: 停止 OTA 通知
 */
class OTACommandDispatcher {
public:
    typedef std::shared_ptr<OTACommandDispatcher> ptr;
    typedef std::function<void()> CommandCallback;

    /**
     * @brief 构造函数
     * @param ota_mgr OTAManager 指针
     */
    explicit OTACommandDispatcher(OTAMqttManager* ota_mgr);

    /**
     * @brief 根据命令字符串获取对应的回调函数
     * @param command 命令类型字符串（如 "notify", "stop_notify"）
     * @return 命令回调函数
     */
    CommandCallback get_cb_by_command(const std::string& command);

    /**
     * @brief 分发命令到对应的处理函数
     * @param command_json JSON 格式的命令字符串
     * 
     * 命令 JSON 格式示例：
     * {
     *   "command": "notify",
     *   "device_type": 1,
     *   "name": "app_name",
     *   "version": "v1.0.0"
     * }
     */
    void dispatch(const std::string& command_json);

private:
    /**
     * @brief 处理 notify 命令
     * @param command_json 命令 JSON 对象
     */
    void handle_notify(const nlohmann::json& command_json);

    /**
     * @brief 处理 stop_notify 命令
     * @param command_json 命令 JSON 对象
     */
    void handle_stop_notify(const nlohmann::json& command_json);

private:
    OTAMqttManager* m_ota_mgr;
    std::unordered_map<std::string, std::function<void(const nlohmann::json&)>> m_command_handlers;
};

}

#endif
