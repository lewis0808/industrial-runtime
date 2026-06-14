# 架构：分层 / 依赖 / 线程 / 数据流

## 1. 分层与最高原则

```
┌─────────────────────────────────────────────────────────────┐
│  接入边界（两个正交的层，互不共用类型）                       │
│   · 上行设备：Plugin C-ABI（irplugin/）  ── 数据「入口」      │
│   · 对外应用：IRSP（../irsp/）           ── 数据「出口」      │
├─────────────────────────────────────────────────────────────┤
│  编排层：RuntimeEngine（组合子系统，实现 RuntimeApi）         │
├─────────────────────────────────────────────────────────────┤
│  子系统：TagEngine · EventBus · MemoryStore · Scheduler      │
├─────────────────────────────────────────────────────────────┤
│  基础：common(类型) · logger · config                        │
└─────────────────────────────────────────────────────────────┘
```

**铁律**（违反即架构腐坏）：

1. **Core 不依赖设备**：core 不得 `#include` 任何设备/协议 SDK（S7、OPC UA、相机 SDK…）。
2. **设备全部插件化**：设备只能经 `RuntimeApi`（`pushTag/pushEvent/pushStream`），
   禁止触碰 TagEngine/EventBus/MemoryStore 内部。
3. **Core 不依赖 IRSP**：IRSP 单向依赖 core 只读接口；core 编译不链接 irsp。
   `IndustrialRuntime` 可执行文件在**根** CMakeLists 把两者组合起来。
4. **Tag 与 Stream 两套独立体系**：实时变量走 Tag，二进制/图像/点云走 Stream，互不转换。

## 2. 模块依赖图（CMake）

```
core_common (INTERFACE，提供 core/ include 根；无任何依赖)
  ├── core_logger         → spdlog
  ├── core_config         → nlohmann_json
  ├── core_tag_engine     → Threads
  ├── core_event_bus      → Threads
  ├── core_memory_store    → Threads
  ├── core_scheduler      → Threads
  ├── core_runtime_engine → tag_engine + event_bus + memory_store + scheduler + config + logger
  ├── core_plugin_host    → core_runtime_engine
  └── core_plugin_manager → core_logger（+ ${CMAKE_DL_LIBS} on POSIX）

IndustrialRuntime (exe，根 CMakeLists) → core_runtime_engine + ... + irsp_server
```

依赖方向**严格单向、无环**。新增模块时：新功能放对应模块，绝不让 `core_common` 依赖子模块
（否则类型根产生环）。`irplugin/` 是 core 自带的 ABI 头**副本**，源出 `sdk/plugin-sdk/`，
升级 SDK 时同步本副本。

## 3. 线程模型

| 线程 | 归属 | 职责 | 退出方式 |
|------|------|------|----------|
| 主线程 | `main` | 引导、信号处理、200ms 轮询等待退出 | `g_running` 原子标志 |
| 事件派发线程 | EventBus | 出队事件 → 扇出订阅者 | `jthread` + `stop_token` + 信号量唤醒 |
| 调度线程 | Scheduler | 到期触发周期任务 | `jthread` + `stop_token` + cv |
| 插件线程 | 各插件 | 设备采集，调 `pushTag/...` | 插件 `stop()` 自行管理 |
| IRSP 服务线程 | irsp（非 core） | WebSocket 服务循环 | libwebsockets |

**跨线程交互的关键点**：

- `pushTag` 在**插件线程**同步执行：写 TagEngine 分片锁 → 若值变化，**在插件线程内同步**
  调用变更回调（IRSP 的 routeTag）。⇒ 慢订阅者会阻塞推送插件（见 [tag-engine 待改善](modules/tag-engine.md)）。
- `pushEvent` 仅入无锁队列后立即返回；真正扇出在派发线程。⇒ 生产者不被订阅者拖慢，
  但队列满会**丢弃**（计数可查）。
- 全部后台线程用 `std::jthread` + `std::stop_token`，**禁止 `detach()`**，所有循环可退出。

## 4. 数据流

### 上行（设备 → 运行时 → 应用）

```
设备插件 ──pushTag──►  RuntimeEngine ──► TagEngine（存储 + 变更回调）──► IRSP routeTag ──► 订阅连接
        ──pushEvent─►  RuntimeEngine ──► EventBus（无锁队列 → 派发线程 → 订阅者）
        ──pushStream►  RuntimeEngine ──► StreamSink（转交 stream/ 模块；当前未接）
```

### 下行（应用写回 → 设备）

```
IRSP SET ──► RuntimeEngine.writeTag ──► WriteHandler ──► PluginHost.write
                                                          └─ 按 topic 前缀线性匹配 ──► 插件 IrPluginWriteFn
```

封送在 `PluginHost`：core 类型 ↔ 纯 C 结构（`IrPluginTagValue` 等），字符串以 `(data,len)`
视图同步传递，回调返回后指针即失效——**宿主在回调内同步拷贝**。

## 5. 跨切面约束（内存 / 异常 / 跨平台）

- **内存**：优先值语义与 `unique_ptr`，禁止裸 `new/delete`（除非与第三方 SDK 交互）。
  插件对象由插件自身堆分配、经 `destroy()` 释放（不跨 DLL 用宿主的 `delete`）。
- **异常**：core 内部异常安全；**异常禁止跨 DLL 边界**。Config 解析失败不抛、返回默认值。
  ⚠️ 当前 Scheduler 任务回调与插件 `init/start` 未在宿主侧做异常防护，见 roadmap。
- **跨平台**：DLL 加载（Windows `LoadLibraryEx` / POSIX `dlopen`）、可执行路径定位、
  动态库扩展名（`.dll`/`.so`/`.dylib`）均已分平台处理。时间戳：Tag/Event 用 `system_clock`
  （墙钟，可序列化），Scheduler 用 `steady_clock`（单调，不受 NTP 跳变影响）。
