# IRSP 命令集（语义层，跨版本恒定）

请求为命令数组（首元素命令名，大小写不敏感），回复见各命令。编码细节见
[encoding/irsp1.md](../encoding/irsp1.md)，错误见 [error.md](error.md)。

约定：`<x>` 必选，`[x]` 可选，`x...` 可重复。回复里的 *TagValue/Event* 指 map 结构
（见 [datatype.md](datatype.md)）。

**示例记法**：`→` 客户端发，`←` 服务端回。请求用 [irsp1 inline 调试形式](../encoding/irsp1.md#inline-命令调试用)
（按空白分词，便于人读；正式客户端仍用 irsp1 编码）。回复为 irsp1，简单串/整数原样写，
map 用 `%n key=value ...` 缩写，二进制值以 `<...>` 注记，`#` 后为说明。完整逐帧线格式见
[examples/session.md](../examples/session.md)。

## 连接管理

| 命令 | 参数 | 回复 | 说明 |
|------|------|------|------|
| `HELLO` | `<version>` `[encoding]` `[transport]` | 服务端能力 map | **必须为首条命令**；协商版本/编码/传输 |
| `AUTH` | `<token>` | `+OK` / `-UNAUTHORIZED` | **预留**（V1 `NOT_IMPLEMENTED`）；握手后、正常通讯前 |
| `PING` | `[payload]` | `+PONG` 或回显 bulk | 应用级保活 |
| `BYE` | — | `+OK` | 优雅关闭 |

连接流程：`连接 → HELLO →（未来 AUTH）→ 正常通讯`。未 `HELLO` 即发其他命令 → `-NOT_READY`。

版本不支持 → `-UNSUPPORTED_VERSION`。

示例：
```
→ HELLO 1
← %5 server="industrial-runtime/1.0.0" version="1" encoding="irsp1" transports="websocket" capabilities="tag,event"

→ PING
← +PONG
→ PING hello                 # 带 payload 时回显
← $5 hello

→ AUTH my-token              # V1 预留
← -NOT_IMPLEMENTED auth reserved

→ BYE
← +OK
```

## Tag 读取

| 命令 | 参数 | 回复 |
|------|------|------|
| `GET` | `<topic>` | *TagValue* map；不存在 → null |
| `MGET` | `<topic>...` | 数组，每元素为 *TagValue* 或 null |
| `EXISTS` | `<topic>` | `:1`（存在）/ `:0`（不存在） |
| `SCAN` | `<cursor>` `<pattern>` `[COUNT <n>]` | `[nextCursor, [topic...]]` |

**EXISTS**：探测某 topic 当前在 Runtime 中是否**有 Tag 记录**，只回真假、不搬运值。
- “存在”= 该精确 topic 已被某插件 `pushTag` 进 TagEngine（运行时实际有数据），**不是**“配置里声明过”。
  故点位虽已配置、但插件尚未首次上推前，`EXISTS` 返回 `:0`。
- 仅**精确名**匹配，**不接受**通配（`+`/`#`）；要按模式列举用 `SCAN`。
- 与 `GET` 的取舍：`GET` 取整条 TagValue（有数据搬运），`EXISTS` 只判在/不在（开销小）。
  典型用途：订阅/写回前确认点位已上线、插件加载后的健康检查。

**SCAN**：游标式增量遍历，替代会卡死的 `KEYS`。
- `cursor` 起始为 `"0"`；回复的 `nextCursor` 为 `"0"` 表示遍历结束。
- `pattern` 为 Topic Tree 模式（`+`/`#`）。`COUNT` 为单批建议条数（提示，非硬保证）。
- 游标**无状态**（自描述 token），客户端可在重连后续传（待确认，见 README 决策点 3）。

示例：
```
→ GET factory1/line1/robot1/temp
← %4 name="factory1/line1/robot1/temp" type="f64" ts=1749800000000000000 value=<8B f64 LE=36.6>
→ GET no/such
← $-1                        # 不存在 → null

→ MGET factory1/line1/robot1/temp no/such
← *2 %4{…temp 的 TagValue…} $-1     # 每元素 TagValue 或 null，按入参顺序

→ EXISTS factory1/line1/robot1/temp
← :1
→ EXISTS no/such
← :0

→ SCAN 0 factory1/# COUNT 100
← *2 "a1f" *2 "factory1/line1/robot1/temp" "factory1/line1/robot1/load"   # [nextCursor, [topic…]]
→ SCAN a1f factory1/#        # 用上一轮 nextCursor 续传
← *2 "0" *1 "factory1/line2/conv1/spd"     # nextCursor=0 → 遍历结束
```

## 订阅（服务端在同连接推送）

| 命令 | 参数 | 回复 | 推送帧 |
|------|------|------|--------|
| `WATCH` | `<topic>...` | `:<当前关注总数>` | `tag` 推送（精确单点） |
| `UNWATCH` | `[topic...]` | `:<剩余数>` | — |
| `SUBSCRIBE` | `<pattern>...` | `:<当前订阅模式数>` | `tag` 推送（子树匹配） |
| `UNSUBSCRIBE` | `[pattern...]` | `:<剩余数>` | — |
| `SUBEVENT` | `[minSeverity]` `[category]` | `:1` | `event` 推送 |
| `UNSUBEVENT` | — | `:0` | — |

- `WATCH` vs `SUBSCRIBE`：前者关注**具体 Tag**（HMI 点位绑定常用），后者关注**子树模式**。
  二者都产生 `tag` 推送帧（见下），由客户端按 `name` 区分。
- 无参 `UNWATCH`/`UNSUBSCRIBE` = 全部取消。

### 推送帧格式

推送帧首字段含类型标记，便于客户端区分「请求回复」与「主动推送」：
- Tag 变化：map，含 `push: "tag"` 标记 + TagValue 全字段。
- 事件：map，含 `push: "event"` 标记 + Event 全字段。

Tag 变化来源 = core `TagEngine` 变更回调（仅值变化触发）；事件来源 = core `EventBus`。

示例：
```
→ WATCH factory1/line1/robot1/temp        # 单点关注
← :1                                       # 回复 = 当前关注总数
→ SUBSCRIBE factory1/#                     # 子树订阅
← :1                                       # 回复 = 当前订阅模式数

# 此后值变化时，服务端在本连接主动推送（带 push 标记，与请求回复区分）：
← %5 push="tag" name="factory1/line1/robot1/temp" type="f64" ts=1749800000000000000 value=<8B f64 LE>

→ SUBEVENT warning alarm                   # 订阅 warning 级以上、alarm 分类
← :1
← %6 push="event" source="s7" category="alarm" severity="warning" ts=… message="high pressure"

→ UNWATCH                                   # 无参 = 全部取消
← :0
→ UNSUBSCRIBE factory1/#                    # 也可指定取消某模式
← :0
→ UNSUBEVENT
← :0
```

## 写回（SET）

| 命令 | 参数 | 回复 |
|------|------|------|
| `SET` | `<topic> <type> <value>` | `+OK` / 错误 |

把设定点下发到设备：`应用 → IRSP → Runtime → 插件(按 topic 前缀归属) → 设备`。
- `type` 为类型标签（`f64`/`i32`/`str`/`bool`…），`value` 为按 type 编码的字节（bulk）。
- **同步「已受理」语义**：插件 `onWrite` 接受（已写出/排队）即回 `+OK`；不代表设备已物理确认。
  设备真正写入后通过正常上行（插件 `pushTag` 回读值）体现。
- 无插件负责该 topic 前缀 → `-NOT_FOUND`；服务端未接写回出口 → `-NOT_IMPLEMENTED`。

示例（`value` 为按 `type` 编码的二进制 bulk，**inline 文本无法准确表达，正式客户端用 irsp1 编码**）：
```
→ SET factory1/line1/robot1/sp f64 <8B f64 LE=42.0>
← +OK                                       # 已受理（已写出/排队），非设备物理确认
→ SET factory1/line1/robot1/temp f64 <8B>   # 该 topic 无插件接管写回
← -NOT_FOUND no writer for topic
```
写值是否真正落到设备，靠之后插件回读上行的 `tag` 推送/`GET` 体现（见上「同步『已受理』语义」）。

> **插件管理不在 IRSP 上**：列举/卸载/重载插件等**有副作用的控制面操作**走**独立的本机 admin
> 通道**（Windows 命名管道 / POSIX AF_UNIX），与 IRSP 数据面解耦（README §7 原则）。
> 协议与用法见 [admin/README.md](../../../admin/README.md)。IRSP 只承载数据面（读/订阅/写回）。

## Stream（V1 预留，未实现）

| 命令 | 参数 | V1 回复 |
|------|------|---------|
| `SUBSTREAM` | `<topic>...` | `-NOT_IMPLEMENTED` |
| `UNSUBSTREAM` | `[topic...]` | `-NOT_IMPLEMENTED` |

协议**保留**这两个命令名与语义占位，使 V2 直接落地 Stream 订阅而无需改动命令体系。
Stream（图像/点云/二进制）吞吐大、需背压，倾向 V2 用**独立二进制推送通道**承载，
而非塞进普通命令回复。

示例：
```
→ SUBSTREAM factory1/cam1/frames
← -NOT_IMPLEMENTED stream subscription is reserved for V2
```

## 命令分类汇总

```
连接:  HELLO  AUTH(预留)  PING  BYE
读取:  GET  MGET  EXISTS  SCAN
订阅:  WATCH  UNWATCH  SUBSCRIBE  UNSUBSCRIBE  SUBEVENT  UNSUBEVENT
写回:  SET
流:    SUBSTREAM(预留)  UNSUBSTREAM(预留)

# 控制面（插件管理等有副作用操作）不在 IRSP 上 —— 走独立本机 admin 通道，见 admin/README.md。
```
