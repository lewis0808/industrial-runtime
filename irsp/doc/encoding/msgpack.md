# 编码 V2 — msgpack

> **状态：已实现**（`codec/msgpack_codec.{hpp,cpp}`）。通过 `HELLO 1 ENCODING msgpack` 协商启用。

## 目标

在**不改动语义层与帧结构语义**的前提下，把值的序列化从 irsp1 的「文本框架 + 小端
二进制 bulk」升级为 [MessagePack](https://msgpack.org)，获得：

- 更紧凑（数值/map 原生编码，省去 CRLF 与十进制长度文本）。
- 原生类型系统（int/float/bin/str/map/array）与 IRSP 数据模型天然对应。
- **数值 value 以原生 int/float 编码**，多语言 SDK 不必再按 `type` 标签解小端字节。

## 类型映射（IrspValue ↔ msgpack）

服务端的中立内存模型为 `IrspValue`，两种编码各自把它转字节。映射如下：

| IrspValue | msgpack | 说明 |
|-----------|---------|------|
| `Null` | `nil`（0xc0） | |
| `Integer`(i64) | int 家族 | 最小宽度编码（fixint/int8…int64/uint8…uint64） |
| `Bulk` | `str` 家族 | 编码方向 bulk 仅承载文本（topic 名 / 游标 / PING 回显 / map key） |
| `Simple` | `str` 家族 | msgpack 无「简单字符串」概念，与 Bulk 同编码 |
| `Error`(code,msg) | `map` `{err: code, msg: msg}` | 约定：SDK 见顶层含 `err` 键的 map 即为错误 |
| `TypedValue`(type,raw) | 依 `type` 见下表 | **TagValue.value 走原生类型** |
| `Array` | array 家族 | |
| `Map` | map 家族（key 为 str） | TagValue / Event / HELLO 等可扩展结构 |

### TypedValue 编码（按类型标签）

`type` 取自 [datatype.md §1](../protocol/datatype.md)；`raw` 为 IRSP 标准字节
（数值小端 / `str` 为 UTF-8）。编码时按 `type` 还原为 msgpack 原生类型：

| type | msgpack |
|------|---------|
| `null` | nil |
| `bool` | true/false |
| `i8`/`i16`/`i32`/`i64` | int（带符号，最小宽度） |
| `u8`/`u16`/`u32`/`u64` | uint（最小宽度） |
| `f32` | float32 |
| `f64` | float64 |
| `str` | str |
| 其它/长度不符 | bin（原样兜底，无损） |

> **类型标签仍保留**：TagValue map 里 `type` 字段照常下发，供 SDK 区分 `i32`/`u32`
> 等宽度/符号差异；`value` 本身则是自描述的 msgpack 数值。

## 解码（请求方向）

服务端只解码客户端请求（命令数组）。解码规则：

- `nil`→Null；`bool`→Integer(0/1)；int/uint 家族→Integer；
- `str`/`bin`→Bulk（命令 token 与二进制 SET 值均落到 Bulk）；
- `float32/64`→Bulk（还原为小端原始字节，便于 SET 透传给 writer）；
- array→Array；map→Map；
- `ext` 及保留字节→坏帧（本协议不使用 ext）。

## 传输内帧界定 / 协商

- **每帧承载一个顶层值**，由 WebSocket 消息边界界定（见 `transport/websocket.md`）。
- 同一服务端同时服务 irsp1 与 msgpack 客户端：服务端按**帧首字节嗅探**编码
  （msgpack 顶层 array/map 首字节落在 `0x80–0x9f` / `0xdc–0xdf`，与 irsp1 的 ASCII
  首字节 `+ - : $ * %` 不重叠），回复/推送按该连接 HELLO 协商出的编码序列化。
- 协商：`HELLO 1 ENCODING msgpack`。缺省沿用客户端成帧方式（纯 msgpack 客户端
  可不带该参数）。HELLO 回复的 `encoding` 字段回显最终编码，`encodings` 字段列出
  服务端支持的全部编码。

## 示例（字节级）

每个代码块为一个 WS 二进制消息载荷的 **msgpack 字节流**。`C→S` 客户端发，`S→C`
服务端发。左列为十六进制字节，右列为注记；字符串值在 fixstr 标签后以 `"..."` 直接给出。
fixstr 标签 = `0xa0 | 长度`，fixarray = `0x90 | n`，fixmap = `0x80 | n`。

> 同一会话的 irsp1 形态见 [examples/session.md](../examples/session.md)，可逐帧对照。

### 1. 握手协商

```
C→S  HELLO 1 ENCODING msgpack
  94                       array(4)
  a5 "HELLO"               fixstr(5)
  a1 "1"                   fixstr(1)   ← 语义版本（恒为 1，与编码无关）
  a8 "ENCODING"            fixstr(8)
  a7 "msgpack"             fixstr(7)
连续字节: 94 a5 48 45 4c 4c 4f a1 31 a8 45 4e 43 4f 44 49 4e 47 a7 6d 73 67 70 61 63 6b
```

```
S→C  HELLO 回复
  86                       map(6)
  a6 "server"        b8 "industrial-runtime/1.0.0"   val fixstr(24)
  a7 "version"       a1 "1"
  a8 "encoding"      a7 "msgpack"                     ← 回显最终编码
  a9 "encodings"     ad "irsp1,msgpack"               ← 服务端支持的全部编码
  aa "transports"    a9 "websocket"
  ac "capabilities"  a9 "tag,event"
```

纯 msgpack 客户端也可省略 `ENCODING msgpack`：只要把 HELLO 帧本身用 msgpack 成帧
（首字节 `94`），服务端嗅探后即按 msgpack 回复并对该连接沿用。

### 2. 读取单个 Tag（GET，**value 原生 f64**）

```
C→S  GET factory1/line1/robot1/temp
  92                       array(2)
  a3 "GET"
  ba "factory1/line1/robot1/temp"      fixstr(26)
```

```
S→C  TagValue 回复（type=f64, value=36.5）
  84                       map(4)
  a4 "name"   ba "factory1/line1/robot1/temp"
  a4 "type"   a3 "f64"
  a2 "ts"     cf <8 字节大端 = 1749800000000000000>     uint64
  a5 "value"  cb 40 42 40 00 00 00 00 00                ← 原生 float64 = 36.5
```

**与 irsp1 的关键差异**就在 `value`：

| 编码 | value 字节 |
|------|-----------|
| irsp1 | `$8\r\n 00 00 00 00 00 40 42 40 \r\n`（8 字节**小端** bulk，需按 `type` 自行解码） |
| msgpack | `cb 40 42 40 00 00 00 00 00`（`cb`=float64 标签 + **大端** IEEE754，自描述） |

`type` 字段照常下发，供区分 `i32`/`u32` 等宽度/符号。

不存在的 Tag：

```
C→S  GET no/such
  92  a3 "GET"  a7 "no/such"
S→C  c0                      nil
```

### 3. 错误（未握手即发命令）

```
C→S  GET a/b/c
S→C  82                      map(2)
  a3 "err"  a9 "NOT_READY"
  a3 "msg"  b4 "HELLO required first"        fixstr(20)
```

SDK 约定：顶层 map 含 `err` 键即为错误。

### 4. Tag 推送

```
S→C  85                      map(5)
  a4 "push"   a3 "tag"
  a4 "name"   ba "factory1/line1/robot1/temp"
  a4 "type"   a3 "f64"
  a2 "ts"     cf <8 字节大端>
  a5 "value"  cb 40 42 40 00 00 00 00 00     原生 float64
```

### 5. 整数标量回复（WATCH / EXISTS 等）

```
C→S  92  a5 "WATCH"  ba "factory1/line1/robot1/temp"
S→C  01                      positive fixint = 1（订阅总数）
```

小整数用 1 字节 fixint，省去 irsp1 的 `:1\r\n`。

## 约束

- **语义层（命令集、字段名、Topic Tree、错误码）完全不变**，仅替换字节编码。
- irsp1 线格保持不变：`TypedValue` 在 irsp1 下仍编码为一段 bulk（与 `Bulk` 等价）。
