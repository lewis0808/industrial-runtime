# 编码 V2 — msgpack（预留）

> **状态：预留，V1 不实现。** 通过 `HELLO ... encoding=msgpack` 协商启用。

## 目标

在**不改动语义层与帧结构语义**的前提下，把值的序列化从 irsp1 的「文本框架 + 小端
二进制 bulk」升级为 [MessagePack](https://msgpack.org)，获得：

- 更紧凑（数值/map 原生编码，省去 CRLF 与十进制长度文本）。
- 原生类型系统（int/float/bin/str/map/array）与 IRSP 数据模型天然对应。
- 自描述，利于多语言 SDK。

## 映射原则（草案）

| IRSP 语义 | irsp1 | msgpack |
|----------|-------|---------|
| 命令请求 | bulk 数组 | array |
| map 结构（TagValue/Event/HELLO） | `%` | map |
| 数值 value | 小端字节 bulk + type 标签 | 原生 int/float（type 标签仍保留以消歧 i32/u32 等宽度/符号） |
| str | bulk | str |
| null | `$-1` | nil |
| 错误 | `-CODE msg` | 约定的 error map，如 `{err: "CODE", msg: "..."}` |

## 约束

- **语义层（命令集、字段名、Topic Tree、错误码）完全不变**，仅替换字节编码。
- 同一服务端应能同时服务 irsp1 与 msgpack 客户端（按连接的 HELLO 协商分别编码）。
- 细节在落地 V2 时补全本文件。
