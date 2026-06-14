# IRP 命令集（语义层，跨版本恒定）

请求为命令数组（首元素命令名，大小写不敏感），回复见各命令。编码细节见
[encoding/resp1.md](../encoding/resp1.md)，错误见 [error.md](error.md)。

约定：`<x>` 必选，`[x]` 可选，`x...` 可重复。回复里的 *TagValue/Event* 指 map 结构
（见 [datatype.md](datatype.md)）。

## 连接管理

| 命令 | 参数 | 回复 | 说明 |
|------|------|------|------|
| `HELLO` | `<version>` `[encoding]` `[transport]` | 服务端能力 map | **必须为首条命令**；协商版本/编码/传输 |
| `AUTH` | `<token>` | `+OK` / `-UNAUTHORIZED` | **预留**（V1 `NOT_IMPLEMENTED`）；握手后、正常通讯前 |
| `PING` | `[payload]` | `+PONG` 或回显 bulk | 应用级保活 |
| `BYE` | — | `+OK` | 优雅关闭 |

连接流程：`连接 → HELLO →（未来 AUTH）→ 正常通讯`。未 `HELLO` 即发其他命令 → `-NOT_READY`。

`HELLO` 回复（map）示例：
```
{ server: "industrial-runtime/1.0.0", version: "1", encoding: "resp1",
  transports: "websocket", capabilities: "tag,event" }
```
版本不支持 → `-UNSUPPORTED_VERSION`。

## Tag 读取

| 命令 | 参数 | 回复 |
|------|------|------|
| `GET` | `<topic>` | *TagValue* map；不存在 → null |
| `MGET` | `<topic>...` | 数组，每元素为 *TagValue* 或 null |
| `EXISTS` | `<topic>` | `:1` / `:0` |
| `SCAN` | `<cursor>` `<pattern>` `[COUNT <n>]` | `[nextCursor, [topic...]]` |

**SCAN**：游标式增量遍历，替代会卡死的 `KEYS`。
- `cursor` 起始为 `"0"`；回复的 `nextCursor` 为 `"0"` 表示遍历结束。
- `pattern` 为 Topic Tree 模式（`+`/`#`）。`COUNT` 为单批建议条数（提示，非硬保证）。
- 游标**无状态**（自描述 token），客户端可在重连后续传（待确认，见 README 决策点 3）。

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

## 写回（SET）

| 命令 | 参数 | 回复 |
|------|------|------|
| `SET` | `<topic> <type> <value>` | `+OK` / 错误 |

把设定点下发到设备：`应用 → IRP → Runtime → 插件(按 topic 前缀归属) → 设备`。
- `type` 为类型标签（`f64`/`i32`/`str`/`bool`…），`value` 为按 type 编码的字节（bulk）。
- **同步「已受理」语义**：插件 `onWrite` 接受（已写出/排队）即回 `+OK`；不代表设备已物理确认。
  设备真正写入后通过正常上行（插件 `pushTag` 回读值）体现。
- 无插件负责该 topic 前缀 → `-NOT_FOUND`；服务端未接写回出口 → `-NOT_IMPLEMENTED`。

## Stream（V1 预留，未实现）

| 命令 | 参数 | V1 回复 |
|------|------|---------|
| `SUBSTREAM` | `<topic>...` | `-NOT_IMPLEMENTED` |
| `UNSUBSTREAM` | `[topic...]` | `-NOT_IMPLEMENTED` |

协议**保留**这两个命令名与语义占位，使 V2 直接落地 Stream 订阅而无需改动命令体系。
Stream（图像/点云/二进制）吞吐大、需背压，倾向 V2 用**独立二进制推送通道**承载，
而非塞进普通命令回复。

## 命令分类汇总

```
连接:  HELLO  AUTH(预留)  PING  BYE
读取:  GET  MGET  EXISTS  SCAN
订阅:  WATCH  UNWATCH  SUBSCRIBE  UNSUBSCRIBE  SUBEVENT  UNSUBEVENT
写回:  SET
流:    SUBSTREAM(预留)  UNSUBSTREAM(预留)
```
