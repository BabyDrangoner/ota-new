# OTA设备端客户端

## 概述

OTA设备端客户端是一个完整的自动化OTA升级框架，用于设备终端与OTA服务端通信，实现自动版本查询、文件下载和组件升级。

## 架构设计

### 核心类

#### 1. OTAComponent - 组件类
表示设备上的一个软件组件（如GPS、MCU、固件等）

**主要功能：**
- 维护组件名称、版本号、升级时间
- 版本号比较
- JSON序列化

#### 2. OTADeviceClient - 设备客户端
代表一个设备终端，负责与服务端通信

**主要功能：**
- MQTT连接与消息订阅
- 处理服务端查询请求
- 处理OTA升级通知
- HTTP文件下载
- MD5文件校验
- 组件升级回调

#### 3. OTADeviceManager - 设备管理器
管理多个设备客户端

**主要功能：**
- 从配置文件加载设备
- 统一管理多个设备
- 批量启动/停止设备
- 全局升级回调设置

## 目录结构

```
sherry/device/
├── ota_component.h          # 组件类头文件
├── ota_component.cc         # 组件类实现
├── ota_device_client.h      # 设备客户端头文件
├── ota_device_client.cc     # 设备客户端实现
├── ota_device_manager.h     # 设备管理器头文件
└── ota_device_manager.cc    # 设备管理器实现

config/
└── ota_device.yaml          # 设备配置文件

tests/
└── test_ota_device_client.cc # 测试程序
```

## 通信协议

### MQTT主题

#### 1. 订阅主题（设备端订阅）

**查询主题：** `/ota/device/query/{device_type}/gps`
- 用途：服务端查询设备版本信息
- QoS：1

**升级通知主题：** `/ota/device/query_download/{device_type}/gps`
- 用途：服务端推送升级通知
- QoS：1

#### 2. 发布主题（设备端发布）

**响应主题：** `/ota/server/query/{device_type}/gps`
- 用途：设备响应查询，上报组件版本
- QoS：1

### 消息格式

#### 查询响应消息

```json
{
  "action": "query",
  "no": 1001,
  "time": "2024-01-01 10:00:00",
  "results": [
    {
      "name": "gps",
      "version": "1.0.0",
      "upgrade_time": "2024-01-01 09:00:00"
    },
    {
      "name": "mcu",
      "version": "1.0.0",
      "upgrade_time": "2024-01-01 09:00:00"
    }
  ]
}
```

#### 升级通知消息（从服务端接收）

```json
{
  "name": "gps",
  "version": "1.1.0",
  "time": "2024-01-01 10:00:00",
  "file_name": "gps_v1.1.0.bin",
  "file_size": 1024000,
  "url_path": "/ota/download/1/gps/1.1.0",
  "md5_value": "5d41402abc4b2a76b9719d911017c592",
  "launch_mode": 0,
  "upgrade_mode": 1
}
```

## 升级流程

1. **设备启动**
   - 连接MQTT服务器
   - 订阅查询和升级通知主题
   - 等待服务端消息

2. **版本查询**
   - 服务端发送查询请求
   - 设备上报当前所有组件版本

3. **升级通知**
   - 服务端发送升级通知（包含新版本信息）
   - 设备检查是否需要升级

4. **文件下载**
   - 从HTTP服务器下载升级文件
   - 保存到本地下载目录

5. **MD5校验**
   - 计算下载文件的MD5值
   - 与服务端提供的MD5对比

6. **执行升级**
   - 调用自定义升级回调函数
   - 执行实际的升级操作（停服务、安装、重启等）

7. **更新版本**
   - 升级成功后更新组件版本号
   - 记录升级时间

## 配置文件

### ota_device.yaml

```yaml
# 设备列表
devices:
  # 设备1
  - device_no: 1001              # 设备编号（唯一）
    device_type: 1               # 设备类型
    mqtt_host: "127.0.0.1"       # MQTT服务器地址
    mqtt_port: 1883              # MQTT服务器端口
    mqtt_protocol: "tcp"         # 协议（tcp/ssl）
    http_host: "127.0.0.1"       # HTTP服务器地址
    http_port: 8020              # HTTP服务器端口
    components:                  # 组件列表
      - name: "gps"
        version: "1.0.0"
      - name: "mcu"
        version: "1.0.0"
```

## 编译

```bash
# 在项目根目录执行
cd build
cmake ..
make test_ota_device_client
```

编译完成后，可执行文件位于 `bin/test_ota_device_client`

## 运行

### 1. 启动服务端

```bash
./bin/test_ota_server
```

### 2. 启动设备端

```bash
# 使用默认配置文件
./bin/test_ota_device_client

# 或指定配置文件
./bin/test_ota_device_client ./config/ota_device.yaml
```

### 3. 运行示例输出

```
========================================
  OTA Device Client Starting...
========================================
Loading devices from: ./config/ota_device.yaml
Device [1001] connected to MQTT broker: 127.0.0.1:1883
Device [1001] subscribed to: /ota/device/query/1/gps
Device [1001] subscribed to: /ota/device/query_download/1/gps
Device added successfully: 1001
...
All devices started successfully!
========================================
Device client is running...
Press Ctrl+C to stop
========================================
```

## 自定义升级逻辑

可以通过设置升级回调函数来实现自定义的升级逻辑：

```cpp
// 自定义升级回调
bool custom_upgrade_callback(const std::string& component_name,
                            const std::string& old_version,
                            const std::string& new_version,
                            const std::string& file_path) {
    // 1. 停止相关服务
    stop_component_service(component_name);
    
    // 2. 备份旧文件
    backup_old_file(component_name);
    
    // 3. 安装新文件
    install_new_file(file_path);
    
    // 4. 重启服务
    restart_component_service(component_name);
    
    // 5. 验证升级结果
    if (verify_upgrade()) {
        return true;  // 升级成功
    }
    
    // 6. 失败则回滚
    rollback_to_old_version();
    return false;
}

// 设置回调
device_manager->set_upgrade_callback(custom_upgrade_callback);
```

## 扩展性设计

### 1. 组件扩展
可以轻松添加新的组件类型，只需在配置文件中定义即可。

### 2. 协议扩展
当前支持MQTT通信，可以扩展支持其他协议（如CoAP、HTTP长轮询等）。

### 3. 升级策略扩展
通过自定义升级回调，可以实现：
- 增量升级
- 差分升级
- A/B分区升级
- 断点续传

### 4. 多设备管理
支持单个进程管理多个设备实例，适合网关场景。

## 特性

✅ **面向对象设计** - 清晰的类层次结构  
✅ **可扩展性** - 易于添加新组件和新功能  
✅ **自动化** - 完全自主运行，无需人工干预  
✅ **可靠性** - MD5校验、错误处理、日志记录  
✅ **灵活性** - 支持自定义升级逻辑  
✅ **多设备支持** - 统一管理多个设备  
✅ **配置驱动** - YAML配置文件，易于维护  

## 注意事项

1. **网络连接**：确保设备能访问MQTT和HTTP服务器
2. **磁盘空间**：确保有足够空间下载升级文件
3. **权限问题**：升级操作可能需要相应的系统权限
4. **版本管理**：建议使用语义化版本号（如1.0.0）
5. **错误处理**：升级失败时应有回滚机制

## 日志

日志输出示例：

```
[INFO] Device [1001] connected to MQTT broker: 127.0.0.1:1883
[INFO] Device [1001] subscribed to: /ota/device/query/1/gps
[INFO] Device [1001] received upgrade notify for component: gps, version: 1.1.0
[INFO] Device [1001] downloading file from: http://127.0.0.1:8020/ota/download/1/gps/1.1.0
[INFO] Device [1001] file downloaded successfully: ./downloads/gps_v1.1.0.bin
[INFO] Device [1001] MD5 verification passed
[INFO] Device [1001] successfully upgraded component: gps from 1.0.0 to 1.1.0
```

## 测试场景

1. **基础功能测试**
   - 设备连接
   - 版本查询响应
   - 升级通知接收

2. **升级流程测试**
   - 文件下载
   - MD5校验
   - 版本更新

3. **异常处理测试**
   - 网络断开
   - 文件损坏
   - MD5不匹配
   - 升级失败回滚

4. **多设备测试**
   - 同时运行多个设备
   - 不同类型设备
   - 不同组件配置

## 未来扩展

- [ ] 支持SSL/TLS加密连接
- [ ] 支持断点续传
- [ ] 支持差分升级
- [ ] 支持升级进度上报
- [ ] 支持设备分组管理
- [ ] 支持灰度升级策略
- [ ] 支持设备状态监控面板

## 联系与支持

如有问题，请查看日志输出或联系开发团队。
