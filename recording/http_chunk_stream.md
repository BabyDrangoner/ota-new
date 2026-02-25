# HTTP Chunked 流式接收实现文档

## 背景

vLLM 等推理服务使用 **HTTP/1.1 Transfer-Encoding: chunked** + **SSE（Server-Sent Events）** 协议向客户端流式推送推理结果。服务端每生成一个 token 就立即写入响应流，客户端无需等待全部推理完成即可处理数据。

原有 `HttpConnection::recvResponse()` 虽然能解析 chunked 格式，但会等所有 chunk 接收完毕后才返回，无法实现边收边处理。本文档描述为此新增的流式接收策略。

---

## 协议基础

### HTTP Chunked 传输格式

```
HTTP/1.1 200 OK
Content-Type: text/event-stream
Transfer-Encoding: chunked        ← 无需 Content-Length

f\r\n                             ← chunk 大小（十六进制）
data: token-1\n\n\r\n             ← chunk 数据 + \r\n
f\r\n
data: token-2\n\n\r\n
...
0\r\n                             ← 终止 chunk（大小为 0）
\r\n
```

整个推理过程只有 **一次 HTTP 请求**，服务端通过持续写入 chunk 实现流式推送，连接在 `0\r\n\r\n` 之前保持不关闭。

### 现有解析器支持情况

| 能力 | 状态 |
|------|------|
| 识别 `Transfer-Encoding: chunked` | ✅ ragel 状态机自动识别 |
| 正确拼接所有 chunk 内容 | ✅ `recvResponse()` 已支持 |
| 每个 chunk 到达时触发回调 | ❌ 原有实现不支持 |
| 流式边收边处理 | ❌ 原有实现不支持 |

---

## 新增接口

### 文件位置

- 头文件：[sherry/http/http_connection.h](../sherry/http/http_connection.h)
- 实现：[sherry/http/http_connection.cc](../sherry/http/http_connection.cc)

### ChunkPolicy — 批次触发策略

```cpp
struct ChunkPolicy {
    size_t max_chunks      = 0;  // 每批最多 N 个 chunk，0 = 不限
    size_t max_buffer_size = 0;  // 每批最多 N 字节，0 = 不限
};
```

**触发条件（满足任意一项即投递当前批次）：**

| 条件 | `is_done` |
|------|-----------|
| 收到终止 chunk（`chunks_done`） | `true` |
| 累积 chunk 数量 ≥ `max_chunks` | `false` |
| 累积字节数 ≥ `max_buffer_size` | `false` |

### ChunkCallback — 回调类型

```cpp
using ChunkCallback = std::function<void(const std::string& data, bool is_done)>;
```

- `data`：本批次累积的 body 内容
- `is_done`：是否是最后一批（收到终止 chunk）

### recvResponseStream() — 流式接收成员方法

```cpp
HttpResponse::ptr recvResponseStream(const ChunkPolicy& policy,
                                     const ChunkCallback& cb);
```

接收 chunked 响应，按策略分批触发 `cb`。返回的 `response->getBody()` 为空（数据已通过回调投递）。

非 chunked 响应自动退化：全量数据通过 `cb(data, true)` 一次性投递。

### DoPostStream() — 静态一体化方法

```cpp
static HttpResult::ptr DoPostStream(
    const std::string& url,
    uint64_t timeout_ms,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ChunkPolicy& policy,
    const ChunkCallback& cb);
```

封装了建连、发请求、流式接收的完整流程，对应原有 `DoPost()` 的流式版本。

---

## 实现逻辑

### 两阶段接收

```
recvResponseStream()
    │
    ├── 第一阶段：解析 HTTP 响应头
    │       do { read → parser->execute(false) } while(!isFinished())
    │       → 解析出 chunked 标志、状态码等
    │
    └── 第二阶段：chunked 数据体流式接收
            │
            ├── 内层循环：等待 ragel 解析出 chunk size 行
            │       do { read → parser->execute(true) } while(!isFinished())
            │       → parser->content_len 设置为当前 chunk 大小
            │
            ├── 读取 content_len 字节的 chunk 数据体
            │       → append 到 batch_body
            │
            ├── 判断触发条件
            │       chunks_done || size_limit || chunk_limit
            │       → 满足则调用 flush(is_done)
            │
            └── 循环直到 chunks_done
```

### batch_body 刷新逻辑

```cpp
auto flush = [&](bool is_done) {
    if(cb) cb(batch_body, is_done);
    batch_body.clear();
    batch_chunks = 0;
};
```

> **注意**：终止 chunk（`content_len == 0`）不计入 `batch_chunks`，但会触发 `flush(true)`。
> 当数据批次正好整除时（如 6 chunks / max_chunks=2），终止 chunk 会单独触发一次空的 `is_done=true` 回调作为流结束信号。

---

## 使用示例

```cpp
// 设置批次策略：每 5 个 chunk 或缓冲超过 4KB 触发一次
HttpConnection::ChunkPolicy policy;
policy.max_chunks      = 5;
policy.max_buffer_size = 4096;

auto result = HttpConnection::DoPostStream(
    "http://127.0.0.1:8000/v1/chat/completions",
    30000,                              // 30s 超时
    {{"Content-Type", "application/json"},
     {"Accept",       "text/event-stream"}},
    requestBody,
    policy,
    [](const std::string& data, bool is_done) {
        // 解析 SSE data 行，提取 token
        parseSseTokens(data);
        if (is_done) {
            onStreamComplete();
        }
    }
);

if (!result || result->result != (int)HttpResult::Error::OK) {
    LOG_ERROR << result->error;
}
```

---

## 数据流时序

```
vLLM Server                TCP 层               recvResponseStream          业务层

    │                        │                         │                      │
    │── HTTP 响应头 ─────────►│                         │                      │
    │                        │── read → execute ───────►│ 解析头，确认 chunked  │
    │── chunk1(token-1) ─────►│                         │                      │
    │── chunk2(token-2) ─────►│                         │                      │
    │                        │── read ─────────────────►│ batch=[t1,t2]        │
    │                        │                         │── cb(batch, false) ──►│
    │── chunk3(token-3) ─────►│                         │                      │
    │── chunk4(token-4) ─────►│                         │                      │
    │                        │── read ─────────────────►│ batch=[t3,t4]        │
    │                        │                         │── cb(batch, false) ──►│
    │      ...               │                         │                      │
    │── 0\r\n\r\n ───────────►│                         │                      │
    │                        │── read ─────────────────►│ chunks_done=true     │
    │                        │                         │── cb("", true) ──────►│
```

---

## 测试

### 测试文件

[tests/test_chunk_stream.cc](../tests/test_chunk_stream.cc)

### 测试结构

```
main() （主线程，IOManager 启动前）
  │
  ├── 原生 POSIX ::socket/::bind/::listen
  │     （必须在 IOManager 前创建，否则被 hook 成非阻塞导致 std::thread 中 accept 失败）
  │
  ├── std::thread ── runServer()
  │       接收请求 → 发 HTTP 响应头
  │       → 每隔 600ms 发一个 chunk（模拟推理延迟）
  │       → 发终止 chunk
  │
  └── IOManager ── runClient()
          DoPostStream(policy{max_chunks=2})
              │
              ├── batch #1: token-1 + token-2  ~1.2s 后到达
              ├── batch #2: token-3 + token-4  ~1.2s 后到达
              ├── batch #3: token-5 + token-6  ~1.2s 后到达
              └── batch #4: ""  is_done=true   终止信号
```

### 验证项

| 验证项 | 说明 |
|--------|------|
| `last callback is_done=1` | 最后一次回调 `is_done` 必须为 true |
| `no intermediate done` | 中间批次 `is_done` 必须为 false |
| `all 6 tokens received` | 所有数据拼合后包含 token-1 到 token-6 |
| `batch count=4` | 3 批数据 + 1 次空 done 信号 |

### 运行

```bash
cd build && make test_chunk_stream
./bin/test_chunk_stream
```

### 预期输出关键行

```
[client] batch #1  time=...  size=30  is_done=0      ← 约 13:xx:01
[client] batch #2  time=...  size=30  is_done=0      ← 约 13:xx:02（间隔 ~1.2s）
[client] batch #3  time=...  size=30  is_done=0      ← 约 13:xx:03（间隔 ~1.2s）
[client] batch #4  time=...  size=0   is_done=1      ← 终止信号
[PASS] last callback is_done=1
[PASS] no intermediate done
[PASS] all 6 tokens received
[PASS] batch count=4 (expected 4: 3 data + 1 empty done)
```

---

## 设计注意事项

### 1. IOManager hook 与 std::thread 冲突

sherry 框架对 `socket`/`connect`/`accept` 等系统调用做了 hook，在 IOManager 启动后创建的 fd 会被设置为非阻塞。`std::thread` 内调用 `::accept` 会立即返回 `EAGAIN`。

**解决方案**：在 `IOManager` 启动前（`main()` 最开始）用原生 POSIX API 创建监听 socket，该 fd 保持阻塞模式，可在 `std::thread` 中正常使用。

### 2. 终止 chunk 的处理

终止 chunk `content_len == 0` 不计入 `batch_chunks`，避免影响批次计数。但它会触发 `flush(true)`，即使 `batch_body` 为空也会投递一次 `cb("", true)` 作为明确的流结束通知。业务层可通过 `is_done=true` 且 `data.empty()` 判断这是纯结束信号。

### 3. 非 chunked 响应兼容

`recvResponseStream` 对非 chunked 响应自动退化为普通接收，全量数据通过 `cb(body, true)` 一次性投递，调用方无需区分两种模式。
