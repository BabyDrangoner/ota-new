# ICP 流式推理实现文档

## 1. 背景

原始实现中，`VllmClient` 以非流式（`DoPost`）方式发起 HTTP 请求，等待 vLLM 返回完整 JSON 后一次性解析。本次改造将整个链路改为流式：

```
vLLM --SSE/chunked--> ICP VllmClient --批次回调--> IcpController --OutputMessage--> Device
```

改造目标：
- vLLM 每推理 N 个 token 即通过 HTTP chunk 送达，ICP 立即解析并通知上层
- ICP 按配置的批次大小聚合 token，一次性打包发给 device
- Device 收到 `stream_batch` 帧，解析 `tokens` 数组，逐个使用

---

## 2. 涉及文件

| 文件 | 类型 |
|---|---|
| `sherry/http/http_connection.h` | HTTP 层：新增流式批次 API |
| `sherry/http/http_connection.cc` | HTTP 层：实现 `recvResponseStream` / `DoPostStream` |
| `sherry/icp/icp_config.h` | 配置：新增流式相关字段 |
| `sherry/icp/icp_vllm_client.h` | 接口：新增 `onStreamBatch` / `doHttpRequestStream` |
| `sherry/icp/icp_vllm_client.cc` | 实现：流式请求主逻辑 |
| `sherry/icp/icp_protocol.h` | 协议：`OutputMessage` 新增 `type` / `tokens` 字段 |
| `sherry/icp/icp_protocol.cc` | 协议：`toJson` / `fromJson` 更新 |
| `tests/test_vllm_stream.cc` | 测试：端到端验证（mock vLLM + VllmClient + device socket） |

---

## 3. HTTP 层：ChunkPolicy + DoPostStream

### 新增结构

```cpp
// sherry/http/http_connection.h

struct ChunkPolicy {
    size_t max_chunks      = 0;  // 0=无限制，N=攒够N个chunk再回调
    size_t max_buffer_size = 0;  // 0=无限制，按字节触发（备用）
};

using ChunkCallback = std::function<void(const std::string& data, bool is_done)>;
```

### 新增 API

```cpp
// 实例方法：流式接收响应
HttpResponse::ptr recvResponseStream(const ChunkPolicy& policy,
                                      const ChunkCallback& cb);

// 静态方法：一站式流式 POST
static HttpResult::ptr DoPostStream(const std::string& url,
                                     uint64_t timeout_ms,
                                     const std::map<std::string, std::string>& headers,
                                     const std::string& body,
                                     const ChunkPolicy& policy,
                                     const ChunkCallback& cb);
```

### 工作原理

```
recvResponseStream() 内部两阶段：
  Phase 1 — 接收并解析响应头（复用 ragel 状态机）
  Phase 2 — chunked 数据循环：
    while (true):
      读 chunk-size 行（hex\r\n）
      读 content_len 字节正文
      append 到 batch_body
      if content_len == 0:              ← 终止 chunk
          flush(is_done=true)
          break
      if ++batch_chunks >= max_chunks:
          flush(is_done=false)
          reset batch

flush(is_done):
    cb(batch_body, is_done)             ← 触发 ChunkCallback
    batch_body.clear(), batch_chunks=0
```

---

## 4. ICP 配置：VllmConfig 新增字段

```cpp
// sherry/icp/icp_config.h

struct VllmConfig {
    // ... 原有字段 ...

    bool     enable_stream       = false;  // 是否启用流式输出
    uint32_t stream_batch_chunks = 1;      // 流式模式下聚合的 chunk 数
                                           //   1 = 逐 chunk 立即回调（最低延迟）
                                           //   N = 攒够 N 个再回调（减少回调次数）
};
```

`stream_batch_chunks` 直接映射到 `ChunkPolicy::max_chunks`，控制 ICP 侧的批次粒度。

---

## 5. VllmClient 流式实现

### VllmCallback 接口变化

```cpp
// sherry/icp/icp_vllm_client.h

class VllmCallback {
public:
    // 原有接口（保持不变）
    virtual void onComplete(const VllmResult& result) = 0;
    virtual void onFirstToken(const std::string& request_id, uint64_t time_ms) {}
    virtual void onStreamToken(const std::string& request_id,
                                const std::string& token) {}

    // 新增：批次回调，一次 HTTP chunk 解析的所有 token 一起通知
    // 默认实现：逐个调用 onStreamToken（向后兼容）
    virtual void onStreamBatch(const std::string& request_id,
                                const std::vector<std::string>& tokens) {
        for (auto& t : tokens) onStreamToken(request_id, t);
    }
};
```

**设计意图**：
- 上层仅需 `onStreamToken` 逐个处理时，不覆盖 `onStreamBatch` 即可，默认路由正确
- 上层需要批次感知（如按批发 device）时，覆盖 `onStreamBatch`，忽略 `onStreamToken`

### doHttpRequest 路由

```cpp
void VllmClient::doHttpRequest(VllmRequest::ptr request) {
    if (m_config.enable_stream) {
        doHttpRequestStream(request);   // ← 流式分支
        return;
    }
    // ... 原有非流式逻辑 ...
}
```

### doHttpRequestStream 核心逻辑

```cpp
void VllmClient::doHttpRequestStream(VllmRequest::ptr request) {
    // ChunkPolicy 直接用配置值
    http::HttpConnection::ChunkPolicy policy;
    policy.max_chunks = m_config.stream_batch_chunks;

    auto chunk_cb = [&](const std::string& data, bool is_done) {
        // 1. 解析 SSE 行，提取 delta.content token 列表
        std::vector<std::string> tokens;
        bool hit_done = parseTokensFromBatch(data, tokens);

        // 2. 遍历，累积 full_text、触发 onFirstToken
        for (auto& tok : tokens) {
            if (first_token) { /* onFirstToken */ }
            full_text += tok;
            ++token_count;
            // LOG: [stream] token #N request_id=... token=[...]
            m_callback->onStreamToken(req_id, tok);
        }

        // 3. 批次回调：把这批 token 一次性通知上层
        if (!tokens.empty()) {
            m_callback->onStreamBatch(req_id, tokens);
        }
    };

    DoPostStream(url, timeout_ms, headers, body, policy, chunk_cb);

    // 4. 推理结束，调用 onComplete
    m_callback->onComplete(vllm_result);
}
```

**注意**：`onStreamToken` 和 `onStreamBatch` 均会被调用，上层根据需要选择实现哪个。

### parseTokensFromBatch 解析逻辑

```
输入：一批 SSE 数据（可能含多行 data: {...}）
输出：token 列表 + 是否遇 [DONE]

逐行扫描:
  if line == "data: [DONE]"  → done=true, break
  if line starts "data: "    → 解析 JSON
    找 "delta" → 找 "content" → 提取引号内字符串
    处理 JSON 转义：\n \t \\ \"
  push token
```

---

## 6. 协议层：OutputMessage 扩展

### 字段变化

```cpp
// sherry/icp/icp_protocol.h

struct OutputMessage {
    uint32_t    car_id;
    uint64_t    seq;
    std::string type;       // "complete" | "stream_token" | "stream_batch"  ← 新增
    std::string status;     // complete 时: "success" | "error"
    uint64_t    latency_ms;
    std::string waypoints;  // complete 时携带完整推理文本
    std::string token;      // stream_token 时的单个 token                   ← 新增
    std::vector<std::string> tokens;  // stream_batch 时的 token 数组         ← 新增
    // ...
};
```

### JSON 格式

**stream_batch 帧**（ICP → Device，每批次一帧）：
```json
{
  "car_id": 1,
  "seq": 3,
  "type": "stream_batch",
  "tokens": ["今", "天", "天"],
  "status": "",
  "latency_ms": 0
}
```

**complete 帧**（推理结束时一帧）：
```json
{
  "car_id": 1,
  "seq": 12,
  "type": "complete",
  "status": "success",
  "latency_ms": 4800,
  "waypoints": "今天天气不错，阳光明媚。"
}
```

### 帧传输格式

沿用 `IcpSession::sendResult` 的格式：

```
+------------------+--------------------+
|  length (4字节)  |  JSON body (N字节) |
+------------------+--------------------+
```

---

## 7. 端到端数据流（stream_batch_chunks = 3）

```
vLLM (mock server)          ICP VllmClient             Device
       |                          |                        |
  400ms 今 ─────────────────────> |                        |
  400ms 天 ─────────────────────> |                        |
  400ms 天 ─────────────────────> |                        |
                              ChunkPolicy 攒满3个          |
                              parseTokensFromBatch        |
                              onStreamBatch(["今","天","天"])
                              sendToDevice ──────────────> stream_batch{tokens:["今","天","天"]}
       |                          |                        |  解析 tokens 数组，逐个打印
  400ms 气 ─────────────────────> |                        |
  400ms 不 ─────────────────────> |                        |
  400ms 错 ─────────────────────> |                        |
                              onStreamBatch(["气","不","错"])
                              sendToDevice ──────────────> stream_batch{tokens:["气","不","错"]}
       |                          |                        |
  ...（共4批次）                  |                        |
  [DONE] ───────────────────────> |                        |
                              onComplete(full_text)        |
                              sendToDevice ──────────────> complete{status:"success", waypoints:"..."}
                                 close write end           |
                                                           | EOF → device 线程退出
```

---

## 8. 测试：test_vllm_stream.cc

### 架构

```
main()
 ├─ 创建 POSIX TCP socket（在 IOManager 前）
 ├─ 创建 socketpair [sv[0]=写端, sv[1]=读端]
 ├─ server_thread: mock vLLM server（每 400ms 发一个 SSE token）
 ├─ device_thread: DeviceReceiver（读 sv[1]，解析 OutputMessage）
 └─ IOManager
      └─ fiber: VllmClient (stream_batch_chunks=3)
```

### 关键参数

| 参数 | 值 |
|---|---|
| mock tokens 数 | 12 |
| chunk 间隔 | 400ms |
| stream_batch_chunks | 3 |
| 预期批次数 | 4（每批 3 token） |
| 预期批次间隔 | ~1200ms |

### 验证项（8 项全 PASS）

| # | 验证点 |
|---|---|
| 1 | VllmClient 侧收到 token 总数 = 12 |
| 2 | Token 内容与 mock 发送一致 |
| 3 | 批次间时间间隔 ≥ `batch_chunks × delay × 0.4` |
| 4 | `onComplete` status = SUCCESS |
| 5 | `onComplete` 拼合文本与预期一致 |
| 6 | Device 侧收到 `stream_token` 记录数 = 12 |
| 7 | Device 侧拼合文本与预期一致 |
| 8 | Device 侧收到 `complete` 帧 |

### 运行方法

```bash
cd /root/xxl/workspace/ota-new
LD_LIBRARY_PATH=./lib ./bin/test_vllm_stream
```

---

## 9. 向后兼容说明

| 场景 | 行为 |
|---|---|
| `enable_stream = false` | 走原有 `DoPost` 非流式路径，无变化 |
| `enable_stream = true, stream_batch_chunks = 1` | 每个 HTTP chunk 触发一次回调，最低延迟 |
| `enable_stream = true, stream_batch_chunks = N` | 攒够 N 个 chunk 再触发，减少回调频率 |
| 只实现 `onStreamToken` 不实现 `onStreamBatch` | `onStreamBatch` 默认路由到 `onStreamToken`，完全兼容 |
| 同时实现 `onStreamBatch` | `onStreamToken` 和 `onStreamBatch` 均被调用，需自行选择处理哪个 |
