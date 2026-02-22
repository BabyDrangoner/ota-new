# ICP 模块实现文档

## 概述

ICP (Image Control Plane) 模块实现了从设备端接收图像数据并与 vLLM 进行推理交互的完整流程。

## 核心特性

- **Latest-Only**: 同一车辆的旧消息/旧推理可被立即作废；系统永远优先处理最新观测
- **单 buffer 覆盖**: 每车仅维护一个 RxSlot，不积压旧消息
- **Early Drop**: 只要从 header 能判断过期（seq/timestamp），立即丢弃整条消息
- **异步 vLLM**: submit/abort/stream 接收都不阻塞控制线程
- **结果只认最新**: 只有 request_id 与当前 car_state 匹配才接受结果

## 文件结构

```
sherry/icp/
├── icp.h                 # 统一头文件
├── icp_protocol.h/cc     # 消息协议定义与解析
├── icp_rx_slot.h/cc      # 每车单槽覆盖缓存
├── icp_car_state.h/cc    # 车辆状态管理
├── icp_config.h/cc       # 配置管理
├── icp_metrics.h/cc      # 可观测性指标
├── icp_vllm_client.h/cc  # vLLM HTTP 客户端
├── icp_controller.h/cc   # 控制平面
└── icp_server.h/cc       # TCP 服务器

tests/
├── test_icp.cc           # 单元测试
├── test_icp_server.cc    # 服务端集成测试
└── test_icp_client.cc    # 客户端模拟器

config/
└── icp.yaml              # 配置示例
```

## 协议格式

### 消息帧结构

```
+---------------+---------------+---------------+---------------+
|   magic (4)   | message_size  |   car_id (4)  |    seq (8)    |
+---------------+---------------+---------------+---------------+
| timestamp_ms  | image_nums(2) | prompt_len(2) |   reserved    |
+---------------+---------------+---------------+---------------+
| ImageMeta 1   | Image Data 1  | ImageMeta 2   | Image Data 2  |
+---------------+---------------+---------------+---------------+
|                    ... more images ...                        |
+---------------+---------------+---------------+---------------+
|                      Prompt (optional)                        |
+---------------+---------------+---------------+---------------+
```

- **magic**: 0x4D504349 ('ICPM')
- **message_size**: 整条消息大小（含 header）
- **car_id**: 车辆 ID（0 到 max_cars-1）
- **seq**: 序列号（单车单调递增）
- **timestamp_ms**: 设备端时间戳（毫秒）
- **image_nums**: 图片数量
- **prompt_len**: prompt 长度（0 表示无）

### ImageMeta 结构

```
+---------------+---------------+
| image_type(1) | reserved (3)  |
+---------------+---------------+
|      image_size (4 bytes)     |
+---------------+---------------+
```

## 模块说明

### 1. icp_protocol - 协议定义

- `MessageHeader`: 消息头结构
- `ImageMeta`: 图片元数据
- `ParsedMessage`: 解析后的消息（零拷贝）
- `MessageParser`: 消息解析器
- `MessageBuilder`: 消息构建器（用于测试）
- `OutputMessage`: 输出消息（ICP → 设备）

### 2. icp_rx_slot - 单槽覆盖缓存

使用版本号方案实现无锁读写：
- IO 写: version++ (odd) → memcpy → version++ (even)
- Control 读: 读到同一个偶数 version 前后不变即可认为一致

### 3. icp_car_state - 车辆状态管理

- 跟踪 `last_seq_seen`: 已见的最大序列号
- 跟踪 `current_request_id`: 当前 inflight 的推理请求
- 跟踪 `inflight`: 是否有正在处理的请求
- 支持 `shouldSubmit()`: 检查是否满足最小提交间隔

### 4. icp_vllm_client - vLLM HTTP 客户端

- OpenAI-compatible HTTP 发送
- `submit(request_id, prompt, images[])`: 提交推理请求
- `abort(request_id)`: 取消推理请求
- 支持流式响应解析
- 异步执行，不阻塞控制线程

### 5. icp_controller - 控制平面

核心调度逻辑：
1. 收到 car_id 通知
2. 读取 RxSlot 的最新 seq
3. 若 seq <= last_seq_seen：return（已处理）
4. 更新 last_seq_seen = seq
5. 若 inflight == true：abort(current_request_id)
6. 生成新 request_id，提交推理

### 6. icp_server - TCP 服务器

- `IcpSession`: 管理单个设备连接
  - 帧解析（按 message_size 拼包）
  - Early Drop（根据 seq 丢弃旧消息）
  - 将完整消息写入 RxSlot
- `IcpServer`: TCP 服务器
  - epoll 接入
  - 管理多个 IcpSession
- `IcpService`: 顶层服务管理器

## 线程模型

```
┌─────────────────┐    ┌─────────────────┐
│ IO Threads (N)  │    │ HTTP Threads (M)│
│                 │    │                 │
│ socket read     │    │ vLLM HTTP       │
│ framing         │    │ requests        │
│ RxSlot write    │    │                 │
└────────┬────────┘    └────────┬────────┘
         │                      │
         │ eventfd/schedule     │ callback
         ▼                      ▼
┌─────────────────────────────────────────┐
│         Control Thread (1~K)            │
│                                         │
│ Latest-Only logic                       │
│ abort old requests                      │
│ submit new requests                     │
│ handle results                          │
└─────────────────────────────────────────┘
```

## 使用示例

```cpp
#include "sherry/icp/icp.h"

using namespace sherry::icp;

int main() {
    // 加载配置
    auto config = IcpConfig::loadFromFile("config/icp.yaml");
    if (!config) {
        config = IcpConfig::getDefault();
    }
    
    // 创建服务
    auto service = std::make_shared<IcpService>(config);
    
    // 初始化
    if (!service->init()) {
        return 1;
    }
    
    // 启动
    service->start();
    
    // ... 运行 ...
    
    // 停止
    service->stop();
    
    return 0;
}
```

## 编译

已添加到 CMakeLists.txt，直接编译即可：

```bash
cd build
cmake ..
make
```

测试程序：
- `bin/test_icp`: 单元测试
- `bin/test_icp_server`: 服务端测试
- `bin/test_icp_client`: 客户端模拟器

## 配置说明

参见 `config/icp.yaml`

## 可观测性指标

### 车辆级指标
- `recv_msgs`: 接收消息数
- `drop_stale`: 丢弃的过期消息数
- `abort_count`: abort 次数
- `submit_count`: 提交次数
- `success_count`: 成功次数
- `error_count`: 错误次数

### 系统级指标
- `inflight_requests`: 正在处理的请求数
- `total_requests`: 累计请求数
- `active_connections`: 活跃连接数
- `io_read_bytes/s`: IO 读取速率

### 延迟指标
- `device_to_submit`: 设备时间戳到提交延迟
- `submit_to_first_token`: 提交到首 token 延迟
- `submit_to_done`: 提交到完成延迟
- `total_e2e`: 端到端延迟

## 验收标准

1. **同一 car 连续发两条消息**：第二条到来后第一条推理必须被 abort；结果只接受第二条
2. **三车并发**：三条请求可同时 in-flight，互不 abort
3. **socket 堆积**：当控制线程处理慢时，RxSlot 覆盖生效，旧消息不入队
4. **结果乱序**：若旧 request 结果晚到，必须丢弃
