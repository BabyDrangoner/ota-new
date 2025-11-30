# OTA Command Dispatcher 使用说明

## 概述

`OTACommandDispatcher` 类用于处理从 Redis 消息队列接收到的 OTA 命令，并将其分发到 `OTAManager` 的相应函数。

## 架构

```
Redis 消息队列 (LPUSH)
    ↓
Redis 消费线程 (BRPOP)
    ↓
redis_message_queue_thread_command_dispatch()
    ↓
OTACommandDispatcher::dispatch()
    ↓
对应的 OTAManager 函数 (异步执行)
```

## 支持的命令类型

### 1. notify - OTA 通知
发起 OTA 升级通知

**JSON 格式：**
```json
{
  "command": "notify",
  "device_type": 1,
  "name": "app_name",
  "version": "v1.0.0"
}
```

**对应函数：** `OTAManager::ota_notify()`

### 2. stop_notify - 停止 OTA 通知
停止正在进行的 OTA 通知

**JSON 格式：**
```json
{
  "command": "stop_notify",
  "device_type": 1,
  "name": "app_name",
  "version": "v1.0.0"
}
```

**对应函数：** `OTAManager::ota_stop_notify()`

### 3. query - 查询设备状态
查询设备的当前状态信息

**JSON 格式：**
```json
{
  "command": "query",
  "device_type": 1,
  "device_no": 12345,
  "action": "status"
}
```

**对应函数：** `OTAManager::ota_query()`

### 4. query_download - 查询下载进度
查询设备的下载进度

**JSON 格式：**
```json
{
  "command": "query_download",
  "device_type": 1,
  "device_no": 12345,
  "name": "app_name"
}
```

**对应函数：** `OTAManager::ota_query_download()`

## 使用方式

### 通过 HTTP 接口推送命令到队列

```cpp
// 在 HTTP handler 中
sd->addServlet("/ota/notify", [](HttpRequest::ptr req, HttpResponse::ptr rsp, HttpSession::ptr session){
    const std::string req_body = req->getBody();
    auto key = OTAHash::get_message_queue_hash();
    return redis_push_message_queue_by_http(redis_pool_name, key, req_body, rsp);
});
```

### 通过 Redis CLI 测试

```bash
# 推送 notify 命令
redis-cli LPUSH ota:device:message '{"command":"notify","device_type":1,"name":"test_app","version":"v1.0.0"}'

# 推送 query 命令
redis-cli LPUSH ota:device:message '{"command":"query","device_type":1,"device_no":12345,"action":"status"}'
```

## 注意事项

1. **异步执行**：所有命令处理都是异步的，通过 `m_command_ioMgr` 进行调度。

2. **无 HTTP 响应**：从 Redis 队列消费的命令无法直接返回 HTTP 响应，因为这是异步处理。如果需要获取执行结果，需要：
   - 通过 MQTT 回调获取设备响应
   - 查询 Redis 中的状态缓存
   - 实现结果通知机制（如 WebSocket、轮询等）

3. **错误处理**：命令处理中的异常会被捕获并记录日志，不会导致线程崩溃。

4. **扩展新命令**：
   - 在 `ota_command_dispatcher.h` 中添加新的 `handle_xxx()` 方法声明
   - 在 `ota_command_dispatcher.cc` 的构造函数中注册新命令
   - 实现对应的处理逻辑

## 示例：添加新命令

```cpp
// 1. 在构造函数中注册
m_command_handlers["reboot"] = [this](const nlohmann::json& cmd) {
    this->handle_reboot(cmd);
};

// 2. 实现处理函数
void OTACommandDispatcher::handle_reboot(const nlohmann::json& command_json) {
    if (!command_json.contains("device_type") || !command_json.contains("device_no")) {
        SYLAR_LOG_ERROR(g_logger) << TAG << " reboot command missing fields";
        return;
    }
    
    uint16_t device_type = command_json["device_type"];
    uint32_t device_no = command_json["device_no"];
    
    // 调用 OTAManager 的相应方法
    // m_ota_mgr->ota_reboot(device_type, device_no);
}
```

## 性能考虑

- **IOManager 线程数**：当前设置为 2 个工作线程，可根据实际负载调整
- **阻塞时间**：Redis BRPOP 设置为永久阻塞（timeout=0），适合低频命令场景
- **并发处理**：多个命令可以并发执行，互不干扰
