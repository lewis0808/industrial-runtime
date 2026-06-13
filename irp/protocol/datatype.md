# IRP 数据模型

本文件定义语义层的数据模型（跨版本恒定）。具体字节如何编码见 `encoding/`。

## 1. 类型标签

Tag 值的类型用简单字符串标签表示，与 `core::DataType` / Plugin ABI 一一对应：

```
null  bool  i8  i16  i32  i64  u8  u16  u32  u64  f32  f64  str
```

值的二进制表示（V1，见 [encoding/resp1.md](../encoding/resp1.md)）：
- 数值类型：定长**小端原始字节**（如 `f64` = 8 字节、`i32` = 4 字节）放入 bulk。
- `str`：UTF-8 原始字节。
- `null`：空值（bulk 长度 -1）。

> 客户端**必须**依据 `type` 字段解码 `value`，不得假设固定宽度以外的内容。

## 2. 可扩展结构原则

所有复合实体（TagValue、Event、未来 StreamMeta）一律用 **map（KV header）** 表达，
**不用位置数组**。理由：未来新增字段（`quality`、`unit`、`source`、`device`…）时，
旧 SDK 按 key 读取、忽略未知 key 即可，**不破坏兼容**。

map 在 V1 RESP 编码中用 `%` 类型（见 encoding）。

## 3. TagValue

**必含 key**：

| key | 类型 | 说明 |
|-----|------|------|
| `name` | str | Tag 名（即 Topic，见 §5） |
| `type` | str | 类型标签（§1） |
| `ts` | i64 | Unix 纪元纳秒 |
| `value` | 依 `type` | 值（二进制/字符串/null） |

**预留可选 key**（V1 可不发；客户端遇到未知 key 须忽略）：
`quality`（如 `good`/`bad`/`uncertain`）、`unit`、`source`（产生该值的插件 id）、
`device`、`seq`（单调序号）。

示例（逻辑视图，字节见 examples）：
```
{ name: "factory1/line1/robot1/temp", type: "f64", ts: 1749800000000000000,
  value: <8 bytes LE>, quality: "good" }
```

## 4. Event

同样用 map。**必含 key**：

| key | 类型 | 说明 |
|-----|------|------|
| `source` | str | 事件来源（插件 id / 模块名） |
| `category` | str | 分类（如 `alarm`/`state`/`system`） |
| `severity` | str | `info`/`warning`/`alarm`/`critical` |
| `ts` | i64 | Unix 纪元纳秒 |
| `message` | str | 人类可读描述 |

预留：`code`（结构化告警码）、`tag`（关联 Tag）、`ack`（是否需确认）。

## 5. Topic 与 Topic Tree

Tag 名即 **Topic**，是 `/` 分隔的层级路径（MQTT 风格）：

```
factory1/line1/robot1/temp
```

订阅模式支持两种通配符（仅用于 `SUBSCRIBE`/`SCAN` 的 pattern）：

| 通配 | 含义 | 示例 | 匹配 |
|------|------|------|------|
| `+` | 单层任意 | `factory1/+/temp` | `factory1/lineA/temp`，不匹配 `factory1/a/b/temp` |
| `#` | 多层任意（**必须是末段**） | `factory1/#` | `factory1` 下任意深度 |

- 服务端用 **Trie（前缀树）** 组织订阅，匹配复杂度与层级深度相关而非 Tag 总数，
  适配十万~百万级 Tag。
- 精确路径（无通配）等价于对单一 Topic 的匹配；单点高频关注请用 `WATCH`（见 command.md）。

> **命名约定（已定稿）**：`Tag Name == Topic`，统一 `/` 分层。**禁止 `.` 分层，禁止
> `.`↔`/` 转换层**（已写入根 `CLAUDE.md` 命名规范）。`.` 仅用于 config 配置路径，与 Tag 无关。
