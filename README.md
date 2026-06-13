# Industrial Runtime（工业数据运行时）

面向**开发者**的工业基础设施（Industrial Middleware）：打破设备与 IT 壁垒，提供统一数据模型、
协议驱动生态、高性能事件总线。**不是** Kepware / Ignition / SCADA / HMI / MES。

```
设备  ──Plugin ABI──►  Core Runtime  ──IRP──►  应用 / 多语言 SDK
(插件)                 (唯一数据中心)          (Web HMI / AI Agent / 集成)
```

- **Core 不依赖设备**，设备全部插件化（C-ABI 边界）。
- **应用全部通过 IRP 接入**（Industrial Runtime Protocol，对外访问协议）。
- **Tag 与 Stream 是两套独立体系**；Tag 走数据总线，Stream 走高带宽通道。

详细规范见 [`CLAUDE.md`](CLAUDE.md) 与各模块下的 `CLAUDE.md` / 文档。

---

## 架构与目录

| 目录 | 角色 | 状态 |
|------|------|------|
| `core/` | 运行时内核：Tag/Event 存储、事件总线、调度、配置、日志、插件宿主 | ✅ 可用 |
| `sdk/plugin-sdk/` | 设备插件开发 SDK（C-ABI + `IPlugin`） | ✅ 可用 |
| `plugins/` | 动态加载的设备插件（含 `example` 示例） | 🟡 仅示例 |
| `irp/` | IRP 协议（规格 + 编解码 + 语义 + WebSocket 服务） | ✅ V1 可用 |
| `sdk/irp-client/` | 多语言客户端 SDK | 🟡 JS + TS |
| `stream/` | 高带宽流数据（图像/点云/二进制） | ⬜ 未开始 |
| `drivers/` | 传统 PLC 驱动（可选） | ⬜ 未开始 |
| `tools/` | 开发工具（format / lint 脚本） | ✅ 可用 |

---

## 已完成

- **Core**：`common` 类型（Variant/TagValue/Event/StreamFrame）、`logger`(spdlog)、`config`(json)、
  `tag_engine`（分片并发）、`event_bus`（无锁 MPMC + 派发线程）、`memory_store`、
  `scheduler`（jthread + stop_token）、`runtime_engine`（编排 + RuntimeApi）、
  `plugin_host` / `plugin_manager`（跨平台 DLL 加载）。
- **Plugin SDK**：`IrPlugin*` C-ABI + `irplugin::IPlugin` / `Host` 封装 + 示例插件，端到端验证。
- **IRP V1**：`resp1` 编解码、Topic Trie（`/` + `+`/`#`）、命令分发与订阅管理、
  `core::TagEngine` 绑定、**libwebsockets 服务**（含跨线程推送）。
- **IRP 客户端**：`sdk/irp-client/JS`（JS，含 HTML 实时监控页）与 `sdk/irp-client/TS`（TypeScript，强类型 + dist）。
- **可运行**：`IndustrialRuntime` 启动 core + IRP/WebSocket(9777) + 心跳演示。
- **测试**：14 个 C++ 单元/集成测试（CTest）+ JS codec 单测，全绿。
- **工具链**：vcpkg 清单、`/utf-8`、`.clang-format` / `.clang-tidy`、`tools/lint.ps1`、
  `compile_commands.json` 导出。

---

## 待实现（Roadmap）

### 1. IRP 协议演进
- [ ] **inline 命令**：非 `*` 开头的文本帧按 Redis 风格内联解析，便于 wscat 手测（`HELLO 1`）。
- [ ] **SET / 写回**：应用→Runtime→插件→设备 的设定点下发与确认语义（V1 已预留命令位）。
- [ ] **Stream over IRP**：落地 `SUBSTREAM/UNSUBSTREAM`（V1 返回 `NOT_IMPLEMENTED`），
      倾向独立二进制推送通道 + 背压。
- [ ] **V2 编码 MessagePack**：帧结构不变，值编码升级（`HELLO encoding=msgpack` 协商）。
- [ ] **V3 传输 TCP/TLV**：高性能裸 TCP（4 字节大端长度前缀），语义不变。
- [ ] **鉴权**：`AUTH`（V1 预留）→ JWT/Token/RBAC/租户。
- [ ] **背压 / 错误路径**：每连接发送队列溢出策略、事件推送 e2e 测试、限流。

### 2. 多语言 SDK（`sdk/irp-client/`）
- [x] **JavaScript**（`sdk/irp-client/JS`，浏览器 + Node，含 HTML 实时监控页）。
- [x] **TypeScript**（`sdk/irp-client/TS`，强类型 + `.d.ts`，tsc 构建）。
- [ ] **Python**（AI Agent / 集成场景，asyncio）。
- [ ] **Java**（企业 / Android）。
- [ ] **C++**（复用 `irp_codec`）。
- [ ] **SDK 自动生成**：由 IRP 命令/类型定义机读生成多语言客户端。

### 3. 设备插件（`plugins/`）
- [ ] 真实插件：`s7` / `modbus` / `opcua` / 相机（`camera`）/ 雷达（`radar`）。
- [ ] 插件采集循环模板（jthread 周期读）+ 配置化。
- [ ] PluginManager 增强：热加载/卸载、健康检查、隔离与崩溃恢复。

### 4. Stream 体系（`stream/`）
- [ ] `stream/` 模块：`Frame` / `PointCloud` / `BinaryBlob` 的高带宽传输与处理。
- [ ] 与 core 的 `pushStream` sink 对接；零拷贝 / 共享内存探索。

### 5. Core 硬化与扩展
- [ ] `TagValue` 增加 `quality`（OPC 风格质量戳）等可选字段（协议已支持 map 扩展）。
- [ ] 持久化 / 历史（可选）；快照与恢复。
- [ ] 性能基准与压测（Tag 吞吐、事件延迟、连接规模）。
- [ ] `drivers/`（传统 PLC，可选）。

### 6. 工程化
- [ ] CI 完善（构建 + CTest + JS test + clang-format/tidy 校验）。
- [ ] 示例配置文件、部署文档、容器化。
- [ ] 跨平台验证（Linux / macOS）。

---

## 构建与运行（速览）

```bash
# 配置 + 构建（CLion 或命令行，vcpkg 工具链）
cmake --preset default && cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure   # 14 个测试

# 运行运行时（IRP 监听 9777）
./cmake-build-debug/IndustrialRuntime

# JS 客户端（浏览器实时监控）
cd sdk/irp-client/JS && node examples/serve.mjs
#   打开 http://localhost:8080/examples/index.html
```

代码风格：`tools/lint.ps1`（clang-format + clang-tidy）。
