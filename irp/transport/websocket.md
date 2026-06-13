# 传输 V1 — WebSocket

V1 默认传输（`HELLO ... transport=websocket`）。

实现选型已定：**libwebsockets**（经 vcpkg，依赖 libuv/openssl/zlib/pthreads）。
握手/掩码/分片/ping-pong/压缩/TLS 由库承担。

线程模型：libwebsockets 为回调式服务循环（`lws_service` 跑在专用线程，
通过 `lws_protocols` 回调处理 ESTABLISHED/RECEIVE/SERVER_WRITEABLE/CLOSED）。
发送须在 `LWS_CALLBACK_SERVER_WRITEABLE` 中进行：先把消息入每连接发送队列，
再 `lws_callback_on_writable(wsi)` 请求可写回调。core 的回调线程
（TagEngine 变更回调、EventBus 派发）跨线程推送时，入队后用
`lws_cancel_service(context)` 唤醒服务线程处理，避免跨线程直接写。

## 连接

- 标准 WebSocket（RFC 6455）握手升级；服务端默认监听端口 **9777**（可配）。
- 路径默认 `/irp`（可配）。子协议（`Sec-WebSocket-Protocol`）建议声明 `irp`。
- 一条 WS 连接 = 一个 IRP 会话。

## 帧映射

- 使用 WebSocket **二进制帧**（opcode 0x2）。**一个 WS 消息 = 一个 IRP 帧**
  （一条请求、或一条回复、或一条推送）。
- WS 自带消息边界，故 IRP 帧**无需再加传输级长度前缀**——编码层（resp1）的顶层值
  直接作为 WS 二进制消息载荷。
- 文本帧（0x1）保留作未来人类可读调试用途，V1 不要求支持。

## 保活与关闭

- 传输级保活用 WS 标准 ping/pong 控制帧。
- 应用级 `PING` 命令（见 command.md）用于探测 IRP 语义层存活。
- 正常关闭：客户端发 `BYE` 后双方走 WS Close 握手；异常断开由 server 清理该连接的
  WATCH/SUBSCRIBE 状态。

## 背压

- 每连接维护发送队列；订阅推送速率超过该连接消费能力时，按「**丢弃 + 计数 + 推送一条
  `-OVERFLOW` 告警**」处理，**不阻塞 core**（与 EventBus 思路一致）。
- 队列容量可配。
