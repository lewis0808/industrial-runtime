# Industrial Runtime（工业数据运行时）

面向**开发者**的工业基础设施（Industrial Middleware）：打破设备与 IT 壁垒，提供统一数据模型、
协议驱动生态、高性能事件总线。**不是** Kepware / Ignition / SCADA / HMI / MES。

```
设备  ──Plugin ABI──►  Core Runtime  ──IRSP──►  应用 / 多语言 SDK
(插件)                 (唯一数据中心)          (Web HMI / AI Agent / 集成)
```

- **Core 不依赖设备**，设备全部插件化（C-ABI 边界）。
- **应用全部通过 IRSP 接入**（Industrial Runtime Serialization Protocol，对外访问协议）。
- **Tag 与 Stream 是两套独立体系**；Tag 走数据总线，Stream 走高带宽通道。

详细规范见 [`CLAUDE.md`](CLAUDE.md) 与各模块下的 `CLAUDE.md` / 文档。

---

## 架构与目录

| 目录 | 角色 | 状态 |
|------|------|------|
| `core/` | 运行时内核：Tag/Event 存储、事件总线、调度、配置、日志、插件宿主 | ✅ 可用 |
| `sdk/plugin-sdk/` | 设备插件开发 SDK（C-ABI + `IPlugin`） | ✅ 可用 |
| （插件） | 设备插件作为**独立工程**开发（拷 SDK 头），编出动态库放进运行时同级 `plugins/` 自动加载 | 🟢 机制可用 |
| `irsp/` | IRSP 协议（规格 + 编解码 + 语义 + WebSocket 服务） | ✅ V1 可用 |
| `sdk/irsp-client/` | 多语言客户端 SDK | 🟡 JS /Python |
| `stream/` | 高带宽流数据（图像/点云/二进制） | ⬜ 未开始 |
| `tools/` | 开发工具（format / lint 脚本） | ✅ 可用 |

---

## 已完成

- **Core**：`common` 类型（Variant/TagValue/Event/StreamFrame）、`logger`(spdlog)、`config`(json)、
  `tag_engine`（分片并发）、`event_bus`（无锁 MPMC + 派发线程）、`memory_store`、
  `scheduler`（jthread + stop_token）、`runtime_engine`（编排 + RuntimeApi）、
  `plugin_host` / `plugin_manager`（跨平台 DLL 加载）。
- **Plugin SDK**：`IrPlugin*` C-ABI（v2）+ `irplugin::IPlugin` / `Host` 封装 + 示例插件，端到端验证。
  数据/host 面纯 C ABI；写回经 `register_writer` / `onWrite`。
- **IRSP V1**：`irsp1` 编解码（+ inline 调试命令）、Topic Trie（`/` + `+`/`#`）、命令分发与订阅管理、
  `core::TagEngine` 绑定、**libwebsockets 服务**（含跨线程推送）。
  命令：HELLO/PING/BYE、GET/MGET/EXISTS/SCAN、WATCH/SUBSCRIBE/SUBEVENT、**SET 写回**。
- **SET 写回**：应用(IRSP SET) → Runtime → 插件(按 topic 前缀归属) → 设备，同步「已受理」语义。
- **IRSP 客户端**：`JS`（含 HTML 实时监控页）、`TS`（强类型 + dist）、`Python`（asyncio，pip wheel）。
- **可运行**：`IndustrialRuntime` 启动 core + 自动发现并加载同级 `plugins/` 下的插件 + IRSP/WebSocket(9777) + 心跳演示，支持 SET。
- **插件自动发现**：扫描可执行文件同级 `plugins/`（动态库，文件名任意）+ `config/`（配置）；设备插件作为独立工程开发（见 `sdk/plugin-sdk/README.md`）。
- **测试**：core / IRSP 的单元与集成测试（CTest）+ JS/TS/Python codec 单测。插件加载/写回的端到端验证随插件移至独立插件工程。
- **工具链**：vcpkg 清单、`/utf-8`、`.clang-format` / `.clang-tidy`、`tools/lint.ps1`、
  `compile_commands.json` 导出。

---

## 待实现（Roadmap）

### 1. IRSP 协议演进
- [x] **inline 命令**：非 `*` 开头的文本帧按 Redis 风格内联解析，便于 wscat 手测（`HELLO 1`）。
- [x] **SET / 写回**：应用→Runtime→插件(按 topic 前缀归属)→设备，同步「已受理」语义；
      插件 `onWrite` + `PluginHost` 前缀路由，端到端测试覆盖。
- [ ] **Stream over IRSP**：落地 `SUBSTREAM/UNSUBSTREAM`（V1 返回 `NOT_IMPLEMENTED`），
      倾向独立二进制推送通道 + 背压。
- [ ] **V2 编码 MessagePack**：帧结构不变，值编码升级（`HELLO encoding=msgpack` 协商）。
- [ ] **V3 传输 TCP/TLV**：高性能裸 TCP（4 字节大端长度前缀），语义不变。
- [ ] **鉴权**：`AUTH`（V1 预留）→ JWT/Token/RBAC/租户。
- [ ] **背压 / 错误路径**：每连接发送队列溢出策略、事件推送 e2e 测试、限流。
- [ ] **INFO / STATS（只读观测）**：暴露 runtime 运行指标（tag 数、事件吞吐 / 丢弃、连接数、
      订阅数、插件状态），供管理控制台与监控读取；**只读**，归入数据面观测（不引入副作用）。

### 2. 多语言 SDK（`sdk/irsp-client/`）
- [x] **JavaScript**（`sdk/irsp-client/JS`，浏览器 + Node，含 HTML 实时监控页）。
- [ ] **TypeScript**（`sdk/irsp-client/TS`，强类型 + `.d.ts`，tsc 构建）。
- [x] **Python**（`sdk/irsp-client/Python`，asyncio，纯 Python，pip wheel）。
- [ ] **Java**（企业）。
- [ ] **C++**（复用 `irsp_codec`）。
- [ ] **Rust**（`sdk/irsp-client/Rust`，管理控制台 TUI 的依赖底座；编解码不内嵌进 TUI）。
- [ ] **SDK 自动生成**：由 IRSP 命令/类型定义机读生成多语言客户端。

### 3. 设备插件（独立工程开发）
- [x] **自动发现 + 配置分离**：runtime 扫描同级 `plugins/`（动态库，文件名任意）自动加载；
      配置取自 `config/<dll basename>.json`（`createPlugin` 透传路径，插件自解析、可热扫描）。
- [x] **插件独立化**：插件不在本仓库内，作为独立工程开发（拷 `sdk/plugin-sdk` 头）。
      参考实现：`industrial-runtime-s7`（S7/snap7，含可复用 `libs7` + 多 PLC + 配置热扫描）。
- [ ] **插件生命周期改纯 C vtable**（COM-lite）：当前 `createPlugin` 返回 C++ `IPlugin*`、
      `init/start/stop/destroy` 是 C++ 虚函数，**实际只支持 C++**；改成 C 函数指针表后才能真正
      "任意语言"写插件（数据/host 面已是纯 C ABI）。
- [ ] 其它真实插件：`modbus` / `opcua` / 相机（`camera`）/ 雷达（`radar`）。
- [~] PluginManager 增强：**按 id 热卸载 / reload 已落地**（owner 归属 + 引用计数排空，写回路径无
      use-after-free；线程安全；经**独立 admin 控制面通道**触发，见 §7）。健康检查、进程外隔离/
      崩溃恢复待做 —— 隔离方案探索见 [core/doc/modules/plugin-out-of-process.md](core/doc/modules/plugin-out-of-process.md)。

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

### 7. 管理控制台与服务化（Ops / 运维侧）

对标 `k9s` / `lazydocker`：一个面板化的运维入口，而非 `mysql` 那样的纯命令 REPL。
关键设计原则：**观测（只读、走 IRSP 数据面）与控制（有副作用、走独立 admin 面）分离**，
避免把控制面命令污染 IRSP 数据总线。

- [ ] **管理控制台 TUI**（Rust + ratatui）：topic 树浏览、实时值 / 推送监控、性能指标面板、
      IRSP 调试查看。**第一版纯观测**——基于 Rust SDK + `INFO/STATS`，core 零改动即可落地。
- [~] **控制面 / Admin 接口**：走**独立 admin 通道**（本机 named pipe / AF_UNIX），与 IRSP 数据面解耦。
      **已落地**：`admin/` 模块 + `irpcli` CLI（`irpcli plugin list/reload/unload`）管理插件生命周期，
      见 [admin/README.md](admin/README.md)。待扩展：改 config / 重配等更多有副作用操作；鉴权
      （token / 管道 ACL）。TUI 第二阶段依赖。
- [~] **运行时动态重配**（控制面前置能力）：config 运行时可写 + 热生效（待做）；
      **插件运行时生命周期已可热卸载/reload**（见 §3），经独立 admin 通道触发。config 热重配仍为启动期一次性。
- [ ] **服务化 / 守护进程**：注册为 Windows 服务 / systemd unit，开机自启、后台运行（`net start` 等）。
      后台化后**无前台控制台**（现 `main.cpp` 的 Ctrl+C / 日志打屏交互消失），
      状态观测与启停均改由 TUI + admin 面承载——故服务化与控制台是配套设计，非独立两件事。

---

## 构建与运行（速览）

```bash
# 配置 + 构建（CLion 或命令行，vcpkg 工具链）
cmake --preset default && cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure   # 17 个测试

# 运行运行时（IRSP 监听 9777）
./cmake-build-debug/IndustrialRuntime

# JS 客户端（浏览器实时监控）
cd sdk/irsp-client/JS && node examples/serve.mjs
#   打开 http://localhost:8080/examples/index.html
```

代码风格：`tools/lint.ps1`（clang-format + clang-tidy）。
