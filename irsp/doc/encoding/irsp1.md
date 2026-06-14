# 编码 V1 — irsp1（RESP 风格 + length-prefixed 二进制）

`irsp1` 是 V1 默认编码（`HELLO ... encoding=irsp1`）。借 RESP「首字节定类型 + CRLF 分隔」，
并以二进制安全 bulk 承载数值/二进制值。

## 类型

| 首字节 | 类型 | 形式 |
|--------|------|------|
| `+` | 简单字符串 | `+OK\r\n` |
| `-` | 错误 | `-<CODE> <message>\r\n` |
| `:` | 整数（int64） | `:1749800000000000000\r\n` |
| `$` | bulk（二进制安全） | `$<len>\r\n<len 字节>\r\n`；`$-1\r\n` = null |
| `*` | 数组 | `*<count>\r\n<元素...>` |
| `%` | **map（KV header）** | `%<pair数>\r\n<key,value 交替...>` |

`%` 是相对 IRSP2 的扩展（对齐 IRSP3 map 思路），用于表达**可扩展结构**
（TagValue / Event / HELLO 回复）。key 为 bulk 字符串，value 为任意上述类型。

## 请求

命令为 bulk 数组，首元素命令名：
```
*2\r\n$3\r\nGET\r\n$27\r\nfactory1/line1/robot1/temp\r\n
```

## 值的二进制表示

数值按类型定长**小端原始字节**放入 `$` bulk；`str` 为 UTF-8；`null` 为 `$-1`：

| type | 字节 |
|------|------|
| `bool` | 1 字节 `00`/`01` |
| `i8/u8` | 1 | `i16/u16` | 2 | `i32/u32/f32` | 4 | `i64/u64/f64` | 8 |
| `str` | 变长 UTF-8 |

客户端依 map 中 `type` 字段解码 `value` bulk。

## Inline 命令（调试用）

为便于人工调试（wscat、浏览器控制台），服务端对**不以 `*` 开头**的请求帧按 Redis 风格
inline 解析：整行按空白拆成参数，等价于一个 bulk 数组。例如直接发文本：

```
HELLO 1
GET system/heartbeat
SUBSCRIBE system/#
```

即可，无需手工构造 `*…$…` 帧。空行被忽略。
说明：仅请求方向支持 inline；**回复仍为 irsp1**（数值为二进制 bulk）。inline 仅供调试，
正式客户端/SDK 一律用 irsp1 编码。不支持引号/转义（按空白切分）。

## TagValue（map）

```
%4\r\n
$4\r\nname\r\n   $26\r\nfactory1/line1/robot1/temp\r\n
$4\r\ntype\r\n   $3\r\nf64\r\n
$2\r\nts\r\n     :1749800000000000000\r\n
$5\r\nvalue\r\n  $8\r\n<8 字节 f64 小端>\r\n
```
未来加 `quality` 等字段 → `%5` 并多一对 KV，旧客户端忽略未知 key，**不破坏兼容**。

## 推送帧

复用 map，额外带 `push` 标记：
```
%5  push="tag"  name=... type=... ts=... value=...
%6  push="event" source=... category=... severity=... ts=... message=...
```

## SCAN 回复

```
*2\r\n
$N\r\n<nextCursor>\r\n
*M\r\n$..\r\n<topic1>\r\n ... 
```

## 注意

- 长度以字节计（二进制安全），不是字符数。
- 一个传输帧承载一个顶层值（请求数组 / 回复 / 推送 map）。传输如何定界见 `transport/`。
