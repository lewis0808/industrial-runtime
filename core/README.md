# Core — 工业数据运行时核心

Core 是 Industrial Runtime 的**唯一数据中心**：接收设备插件推送的 Tag / Event / Stream，
统一存储与分发。**不依赖任何具体设备**——设备一律通过插件经 `RuntimeApi` 接入；
应用一律通过 [IRSP](../irsp/README.md) 接入。

> 设计约束（AI 生成代码须遵守）见 [`CLAUDE.md`](CLAUDE.md)。本目录文档面向**人类读者**，
> 讲清每个模块的职责、实现要点与**待改善**；规格性约束以 `CLAUDE.md` 为准。

## 文档地图

| 文件 | 内容 |
|------|------|
| [doc/architecture.md](doc/architecture.md) | 分层、依赖图、数据流、线程模型、内存/异常/跨平台约束 |
| [doc/data-model.md](doc/data-model.md) | `common/` 数据模型：Variant/DataType/TagValue/Event/StreamFrame，三套体系边界 |
| [doc/modules/tag-engine.md](doc/modules/tag-engine.md) | TagEngine：分片并发 Tag 存储 + 变更回调 |
| [doc/modules/event-bus.md](doc/modules/event-bus.md) | EventBus + 无锁 MPMC 队列：事件投递与扇出 |
| [doc/modules/memory-store.md](doc/modules/memory-store.md) | MemoryStore：通用内存 KV |
| [doc/modules/scheduler.md](doc/modules/scheduler.md) | Scheduler：可中断周期调度 |
| [doc/modules/config.md](doc/modules/config.md) | Config：JSON + 点号路径配置 |
| [doc/modules/logger.md](doc/modules/logger.md) | Logger：spdlog 封装 |
| [doc/modules/runtime-engine.md](doc/modules/runtime-engine.md) | RuntimeEngine：编排层 + RuntimeApi |
| [doc/modules/plugin-system.md](doc/modules/plugin-system.md) | 插件系统：ABI 契约 + PluginHost 封送 + PluginManager 生命周期 |
| [doc/roadmap.md](doc/roadmap.md) | **待改善**汇总（按优先级，跨模块） |

## 架构一览

```
                 ┌──────────────── 应用 / 多语言客户端 ────────────────┐
                 │                    IRSP (WebSocket)                  │
                 └───────────────────────┬─────────────────────────────┘
                                         │ 读 Tag / 订阅 / SET 写回
                                ┌────────▼────────┐
   设备插件(.dll/.so) ──C ABI──►│  RuntimeEngine  │  组合，非继承
   pushTag/Event/Stream         │  (实现 RuntimeApi)│
                                └───┬───┬───┬───┬──┘
                          ┌─────────┘   │   │   └──────────┐
                   ┌──────▼─────┐ ┌─────▼────┐ ┌▼────────┐ ┌▼──────────┐
                   │ TagEngine  │ │ EventBus │ │MemStore │ │ Scheduler │
                   │ 分片+RWLock│ │无锁队列+ │ │ 分片KV  │ │ jthread   │
                   │ 变更回调   │ │派发线程  │ │         │ │ 周期任务  │
                   └────────────┘ └──────────┘ └─────────┘ └───────────┘
```

- **Tag 与 Stream 是两套独立体系**，互不混用（实时变量走 Tag，图像/点云走 Stream）。
- Core 各模块为独立 STATIC 库，依赖边集中在 `core/CMakeLists.txt`；`core_common` 是无依赖的
  类型根，**不得**反向依赖任何子模块。
- 详细分层、依赖图、线程与数据流见 [doc/architecture.md](doc/architecture.md)。

## 模块速查

| 模块 | 库目标 | 一句话 | 关键并发原语 |
|------|--------|--------|--------------|
| `common/` | `core_common` (INTERFACE) | 跨模块共享类型（仅头文件） | — |
| `logger/` | `core_logger` | spdlog 封装，宏 `IR_LOG_*` | spdlog 内部线程安全 |
| `config/` | `core_config` | nlohmann/json + 点号路径 | `shared_mutex` |
| `tag_engine/` | `core_tag_engine` | 16 分片 Tag 存储 + 变更回调 | 每片 `shared_mutex` |
| `event_bus/` | `core_event_bus` | 无锁队列 + 单派发线程扇出 | Vyukov MPMC + `jthread` + `semaphore` |
| `memory_store/` | `core_memory_store` | 通用内存 KV | 16 分片 `shared_mutex` |
| `scheduler/` | `core_scheduler` | 可中断周期调度 | `jthread` + `condition_variable_any` |
| `runtime_engine/` | `core_runtime_engine` | 编排 + RuntimeApi | 组合上述全部 |
| `plugin_host/` | `core_plugin_host` | RuntimeApi → C-ABI 宿主 | 静态 thunk 封送 |
| `plugin_manager/` | `core_plugin_manager` | 跨平台 DLL 加载 + 生命周期 | 调用方串行 |
| `irplugin/` | (头副本) | 宿主↔插件 C-ABI 契约 | — |

## 当前状态

- ✅ 数据面贯通：插件 `pushTag/Event/Stream` → RuntimeEngine → TagEngine/EventBus/StreamSink。
- ✅ 写回链路：IRSP `SET` → RuntimeEngine.writeTag → PluginHost 按 topic 前缀路由 → 插件 `onWrite`。
- ✅ 插件自动发现：扫描 `<exe>/plugins/` 加载，配置取 `<exe>/config/<basename>.json`。
- ⚠️ **插件生命周期仍是 C++ vtable**（`IPlugin`），数据面才是纯 C ABI —— 真正的「任意语言/编译器」
  插件尚未达成，见 [roadmap](doc/roadmap.md)。
- ⏳ `stream/` 流处理模块尚未接入（`pushStream` 仅转交可选 sink，`main` 未注册 sink）。

## 构建与测试

- 依赖经 vcpkg 清单模式提供（`vcpkg.json`：`spdlog`、`nlohmann-json`）。
- MSVC 中文 locale 需 `/utf-8`（根 `CMakeLists` 已配），否则 UTF-8 注释被按 GBK 解析编译错乱。
- 测试：CTest + `tests/test_util.hpp` 极简断言（零外部框架），每测试独立可执行。
  `ctest --test-dir cmake-build-debug --output-on-failure`。
