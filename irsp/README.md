# IRSP — Industrial Runtime Serialization Protocol

IRSP 是 Industrial Runtime 的**对外统一访问协议**（定位类比 Redis RESP，但面向工业数据）。
应用 / 多语言客户端（`sdk/irsp-client/`）通过它从 Runtime **读取 Tag、订阅 Tag 变化、
订阅事件**（未来含 Stream）。它是数据的「出口」，与设备侧 **Plugin ABI**（数据「入口」）正交。

> 状态：**V1 规格已定稿，进入实现阶段**。规格文档在 `doc/`，实现见 `codec/`（编解码）、`semantic/`（命令语义）、`server/`（WebSocket 服务）。

## 文档地图

| 文件 | 内容 |
|------|------|
| [doc/protocol/datatype.md](doc/protocol/datatype.md) | 数据模型：类型标签、可扩展 TagValue/Event 结构、Topic Tree、命名约定 |
| [doc/protocol/command.md](doc/protocol/command.md) | 命令集（语义层，跨版本恒定） |
| [doc/protocol/error.md](doc/protocol/error.md) | 错误码表 |
| [doc/transport/websocket.md](doc/transport/websocket.md) | V1 传输：WebSocket |
| [doc/transport/tcp.md](doc/transport/tcp.md) | V3 传输：TCP/TLV（预留） |
| [doc/encoding/irsp1.md](doc/encoding/irsp1.md) | V1 编码：RESP 风格 + 二进制 bulk |
| [doc/encoding/msgpack.md](doc/encoding/msgpack.md) | V2 编码：MessagePack（预留） |
| [doc/examples/session.md](doc/examples/session.md) | 完整会话示例 |

## 分层与版本演进

**语义层恒定；编码层与传输层可独立演进**，连接时由 `HELLO` 协商。

```
┌───────────────────────────────────────────────┐
│ 语义层  命令集 / 数据模型 / 错误模型（恒定）     │  HELLO, GET, WATCH, SUBSCRIBE...
├───────────────────────────────────────────────┤
│ 编码层  帧内值如何序列化（可换）                │  V1 IRSP+bin → V2 MessagePack
├───────────────────────────────────────────────┤
│ 传输层  字节怎么传（可换）                      │  V1 WebSocket → V3 TCP/TLV
└───────────────────────────────────────────────┘
```

| 版本 | 传输 | 编码 | 语义 |
|------|------|------|------|
| **V1** | WebSocket | RESP 风格 + length-prefixed 二进制 | 基线命令集 |
| **V2** | WebSocket | 帧不变，值编码升级 MessagePack | **不变** |
| **V3** | 新增 TCP/TLV | 高性能二进制 | **不变** |

## 设计原则（已确认）

- ✓ V1 传输 **WebSocket**；编码 **IRSP1**；**MessagePack 预留**（V2）。
- ✓ **HELLO 强制**：连接 → HELLO →（未来 AUTH）→ 正常通讯。
- ✓ **Core 不依赖 IRSP**；IRSP 单向依赖 core 只读接口。**插件不感知 IRSP**。
- ✓ **SDK 由 IRSP 定义生成**：命令/类型定义需精确到可（未来）机读自动产出多语言客户端。
- ✓ 数据结构**可扩展**（map / KV header），新增字段不破坏既有 SDK。

## 相对初稿的修订（按评审收敛）

- ✗ `KEYS *` → **`SCAN cursor pattern COUNT`**（游标迭代，避免百万 Tag 时卡死）。
- ✗ Glob → **Topic Tree**（MQTT 式 `/` 分层、`+`/`#` 通配，Trie 友好）。
- ✗ Stream 完全移除 → **预留 `SUBSTREAM/UNSUBSTREAM`**（V1 返回 `NOT_IMPLEMENTED`）。
- ✗ TagValue 位置数组 → **可扩展 map 结构**；Event 同此原则。
- ＋ 新增 **`WATCH`**（单点订阅，区别于 `SUBSCRIBE` 子树）。

## 决策定稿

1. **Topic 分隔符 = `/`**：`Tag Name == Topic`，统一 `/` 分层（`factory1/line1/+/temp`、
   `factory1/#`）。**禁止 `.` 分层，禁止 `.`↔`/` 转换层**。已写入根 `CLAUDE.md` 命名规范，
   全项目示例 Tag 已统一为 `/`（如 `example/temperature`）。
2. **默认端口 9777**（易记：1883 MQTT / 4840 OPC UA / 6379 Redis / 9777 IRSP）。
3. **SCAN 无状态游标**：游标为自描述 token，服务端不保存 `connection→cursor` 状态，
   利于未来 HA / Cluster / Gateway。
4. **WebSocket 实现 = libwebsockets**（经 vcpkg，依赖 libuv/openssl/zlib/pthreads）。
   工业界久经考验的 C 库；回调式服务循环，跨线程推送用 `lws_cancel_service` 唤醒 +
   每连接发送队列 + `lws_callback_on_writable`。
   （websocketpp 已从上游 vcpkg 移除；最终选 libwebsockets 而非 uWebSockets。）
5. **HELLO 强制**；**AUTH 预留**、**SUBSTREAM/UNSUBSTREAM 预留**（V1 返回 `NOT_IMPLEMENTED`）。
6. **可扩展 map 结构**保持（TagValue/Event）。
