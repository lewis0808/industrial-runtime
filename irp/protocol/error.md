# IRP 错误模型

## 错误帧

错误以 RESP 错误类型返回：`-<CODE> <message>\r\n`（V2 起 message 可走 MessagePack）。
`CODE` 为大写枚举，`message` 为人类可读补充，客户端**按 CODE 编程，按 message 显示**。

示例：`-WRONG_ARITY GET requires exactly 1 argument`

## 错误码表

| CODE | 含义 | 是否关闭连接 |
|------|------|------------|
| `ERR` | 通用/未分类错误 | 否 |
| `UNKNOWN_COMMAND` | 未知命令 | 否 |
| `WRONG_ARITY` | 参数个数不对 | 否 |
| `NOT_READY` | 未先 `HELLO` | 否 |
| `UNSUPPORTED_VERSION` | `HELLO` 协商版本不支持 | 是 |
| `NOT_FOUND` | 目标不存在（语义性，区别于 `GET` 的 null 回复） | 否 |
| `NOT_IMPLEMENTED` | 命令预留但本版本未实现（如 V1 的 `SUBSTREAM`/`AUTH`） | 否 |
| `UNAUTHORIZED` | 未认证（未来鉴权） | 否 |
| `FORBIDDEN` | 已认证但无权限（未来 RBAC/租户） | 否 |
| `TIMEOUT` | 操作超时 | 否 |
| `BUSY` | 服务端过载/限流 | 否 |
| `OVERFLOW` | 推送队列溢出（告警，非致命） | 否 |
| `INTERNAL_ERROR` | 服务端内部错误 | 否 |
| `PROTOCOL_ERROR` | 帧解析失败 / 协议违规 | 是 |

## 约定

- **协议级错误**（坏帧、`PROTOCOL_ERROR`、`UNSUPPORTED_VERSION`）→ 服务端关闭连接。
- **命令级错误**（参数、未知命令、未实现等）→ 返回错误帧，连接保留。
- `GET` 命中不存在的 Tag 返回 **null 值**（正常回复），不是 `NOT_FOUND`；
  `NOT_FOUND` 用于语义性"资源不存在"场景（未来命令）。
- 错误码集合为**预留全集**，V1 可能仅实际产生其中一部分；客户端应能容忍未来新增码。
