# ICP 通信示例

本目录包含 ICP Server 和 Device Client 的独立启动程序及测试脚本。

## 文件结构

```
sample/icp/
├── README.md                 # 本文档
├── icp_server_main.cc       # ICP Server 主程序
├── device_client_main.cc    # 设备客户端主程序
├── run_server.sh            # 服务器启动脚本
├── run_device.sh            # 设备客户端启动脚本
└── run_test.sh              # 完整测试脚本
```

## 编译

```bash
cd /root/xxl/workspace/ota-new/build
make icp_server device_client
```

编译完成后，可执行文件位于 `bin/` 目录:
- `bin/icp_server`
- `bin/device_client`

## 使用方法

### 方法一：手动启动两个终端

**终端1 - 启动 ICP Server:**
```bash
./sample/icp/run_server.sh 9000
# 或直接运行
./bin/icp_server 9000
```

**终端2 - 启动 Device Client:**
```bash
./sample/icp/run_device.sh 127.0.0.1 9000 1
# 或直接运行
./bin/device_client 127.0.0.1 9000 1
```

### 方法二：使用自动化测试脚本

```bash
./sample/icp/run_test.sh [port] [duration] [num_devices]

# 示例:
./sample/icp/run_test.sh 9000 10 1   # 端口9000, 运行10秒, 1个设备
./sample/icp/run_test.sh 9000 30 3   # 端口9000, 运行30秒, 3个设备
```

测试脚本会:
1. 启动 ICP Server
2. 启动指定数量的 Device Client
3. 运行指定时长
4. 自动清理进程
5. 显示日志摘要

日志保存在 `sample/icp/logs/` 目录。

## 参数说明

### ICP Server
```
./bin/icp_server [port]

参数:
  port  - 监听端口 (默认: 9000)
```

### Device Client
```
./bin/device_client [server_ip] [server_port] [car_id]

参数:
  server_ip   - 服务器IP地址 (默认: 127.0.0.1)
  server_port - 服务器端口 (默认: 9000)
  car_id      - 车辆ID (默认: 1)
```

## 通信流程

```
┌─────────────────┐              ┌─────────────────┐
│  Device Client  │              │   ICP Server    │
│   (车载设备)     │              │   (边缘服务器)   │
└────────┬────────┘              └────────┬────────┘
         │                                │
         │  1. TCP Connect                │
         │ ─────────────────────────────> │
         │                                │
         │  2. ICP Message                │
         │  [Header + Images + Prompt]    │
         │ ─────────────────────────────> │
         │                                │
         │  3. Process (vLLM inference)   │
         │                                ├──┐
         │                                │  │
         │                                │<─┘
         │                                │
         │  4. OutputMessage (JSON)       │
         │  [car_id, seq, waypoints...]   │
         │ <───────────────────────────── │
         │                                │
         │  5. 循环发送                    │
         │ ─────────────────────────────> │
         │                                │
```

## 预期输出

### ICP Server
```
ICP Server 已启动，监听端口 9000
[新连接] car_id=1 已注册
[收到消息] car_id=1 seq=1 images=2
[发送结果] car_id=1 seq=1 status=success
```

### Device Client
```
设备已启动，正在连接服务器...
[ICP Result] car_id=1 seq=1 status=success latency=50ms waypoints=5
  路径点:
    [0] x=1.0 y=2.0 z=0.0
    [1] x=3.0 y=4.0 z=0.0
    ...
```

## 注意事项

1. **vLLM 模拟**: 当前 ICP Server 使用模拟的 vLLM 响应，实际部署时需要配置真实的 vLLM 服务端点

2. **图像数据**: Device Client 使用模拟图像数据（1张 RGB + 1张深度图）

3. **网络**: 默认使用 localhost (127.0.0.1)，如需跨机器测试，请修改 IP 地址

4. **信号处理**: 程序支持 Ctrl+C (SIGINT) 优雅退出
