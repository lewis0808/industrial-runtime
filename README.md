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
| `plugins/` | 动态加载的设备插件（`example` + `s7` 真 snap7） | 🟢 example + S7(snap7) |
| `irp/` | IRP 协议（规格 + 编解码 + 语义 + WebSocket 服务） | ✅ V1 可用 |
| `sdk/irp-client/` | 多语言客户端 SDK | 🟡 JS /Python |
| `stream/` | 高带宽流数据（图像/点云/二进制） | ⬜ 未开始 |
| `drivers/` | 传统 PLC 驱动（可选） | ⬜ 未开始 |
| `tools/` | 开发工具（format / lint 脚本） | ✅ 可用 |

---

## 已完成

- **Core**：`common` 类型（Variant/TagValue/Event/StreamFrame）、`logger`(spdlog)、`config`(json)、
  `tag_engine`（分片并发）、`event_bus`（无锁 MPMC + 派发线程）、`memory_store`、
  `scheduler`（jthread + stop_token）、`runtime_engine`（编排 + RuntimeApi）、
  `plugin_host` / `plugin_manager`（跨平台 DLL 加载）。
- **Plugin SDK**：`IrPlugin*` C-ABI（v2）+ `irplugin::IPlugin` / `Host` 封装 + 示例插件，端到端验证。
  数据/host 面纯 C ABI；写回经 `register_writer` / `onWrite`。
- **IRP V1**：`resp1` 编解码（+ inline 调试命令）、Topic Trie（`/` + `+`/`#`）、命令分发与订阅管理、
  `core::TagEngine` 绑定、**libwebsockets 服务**（含跨线程推送）。
  命令：HELLO/PING/BYE、GET/MGET/EXISTS/SCAN、WATCH/SUBSCRIBE/SUBEVENT、**SET 写回**。
- **SET 写回**：应用(IRP SET) → Runtime → 插件(按 topic 前缀归属) → 设备，同步「已受理」语义。
- **IRP 客户端**：`JS`（含 HTML 实时监控页）、`TS`（强类型 + dist）、`Python`（asyncio，pip wheel）。
- **可运行**：`IndustrialRuntime` 启动 core + 按配置加载插件 + IRP/WebSocket(9777) + 心跳演示，支持 SET。
- **测试**：16 个 C++ 单元/集成测试（CTest）+ JS/TS/Python codec 单测，全绿。
- **工具链**：vcpkg 清单、`/utf-8`、`.clang-format` / `.clang-tidy`、`tools/lint.ps1`、
  `compile_commands.json` 导出。

---

## 待实现（Roadmap）

### 1. IRP 协议演进
- [x] **inline 命令**：非 `*` 开头的文本帧按 Redis 风格内联解析，便于 wscat 手测（`HELLO 1`）。
- [x] **SET / 写回**：应用→Runtime→插件(按 topic 前缀归属)→设备，同步「已受理」语义；
      插件 `onWrite` + `PluginHost` 前缀路由，端到端测试覆盖。
- [ ] **Stream over IRP**：落地 `SUBSTREAM/UNSUBSTREAM`（V1 返回 `NOT_IMPLEMENTED`），
      倾向独立二进制推送通道 + 背压。
- [ ] **V2 编码 MessagePack**：帧结构不变，值编码升级（`HELLO encoding=msgpack` 协商）。
- [ ] **V3 传输 TCP/TLV**：高性能裸 TCP（4 字节大端长度前缀），语义不变。
- [ ] **鉴权**：`AUTH`（V1 预留）→ JWT/Token/RBAC/租户。
- [ ] **背压 / 错误路径**：每连接发送队列溢出策略、事件推送 e2e 测试、限流。

### 2. 多语言 SDK（`sdk/irp-client/`）
- [x] **JavaScript**（`sdk/irp-client/JS`，浏览器 + Node，含 HTML 实时监控页）。
- [x] **TypeScript**（`sdk/irp-client/TS`，强类型 + `.d.ts`，tsc 构建）。
- [x] **Python**（`sdk/irp-client/Python`，asyncio，纯 Python，pip wheel）。
- [ ] **Java**（企业 / Android）。
- [ ] **C++**（复用 `irp_codec`）。
- [ ] **SDK 自动生成**：由 IRP 命令/类型定义机读生成多语言客户端。

### 3. 设备插件（`plugins/`）
- [ ] **插件生命周期改纯 C vtable**（COM-lite）：当前 `createPlugin` 返回 C++ `IPlugin*`、
      `init/start/stop/destroy` 是 C++ 虚函数，**实际只支持 C++**；改成 C 函数指针表后才能真正
      "任意语言"写插件（数据/host 面已是纯 C ABI）。
- [x] **`s7` 插件（真 snap7）**：jthread 周期采集 + 写回 + S7 大端 DB 编解码；`Snap7Backend`
      用 snap7 `Cli_*` 真 TCP/S7comm 连 PLC；端到端测试用 snap7 `Srv_*` 起虚拟 PLC（无需硬件）。
      `S7Backend` 抽象保留内存模拟备选。
- [ ] 其它真实插件：`modbus` / `opcua` / 相机（`camera`）/ 雷达（`radar`）。
- [ ] 插件**配置化**：把 PLC 地址 / tag 映射 / 轮询周期等从内置表改为配置驱动
      （需插件配置传递机制）。
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
ctest --test-dir cmake-build-debug --output-on-failure   # 16 个测试

# 运行运行时（IRP 监听 9777）
./cmake-build-debug/IndustrialRuntime

# JS 客户端（浏览器实时监控）
cd sdk/irp-client/JS && node examples/serve.mjs
#   打开 http://localhost:8080/examples/index.html
```

代码风格：`tools/lint.ps1`（clang-format + clang-tidy）。
