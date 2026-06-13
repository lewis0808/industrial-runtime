# Core Runtime

工业数据运行时的核心：唯一数据中心。负责接收插件推送的 Tag/Event/Stream，
统一存储与分发。**不依赖任何具体设备**，设备一律通过插件经 `RuntimeApi` 接入。

## 目录

```markdown
core/
├── common/                # 跨模块共享类型（仅头文件，无依赖）
├── logger/                # spdlog 封装
├── config/                # nlohmann/json 配置系统
├── tag_engine/            # Tag 内存模型（分片并发）
├── event_bus/             # 高性能无锁事件总线
├── memory_store/          # 内存 KV（类 Redis 核心）
├── scheduler/             # 周期任务调度（采集周期）
├── runtime_engine/        # 编排层 + 对插件可见的 Runtime API
├── plugin_host/           # RuntimeApi -> C-ABI 宿主接口适配
├── plugin_manager/        # 跨平台 DLL 加载 + 插件生命周期
├── tests/                 # 单元测试（CTest，零外部框架）
└── main.cpp               # 运行时入口
```

> 注：`common/` 不在最初规划目录中，是后加的。Tag/Event/Stream 等类型需要一个
> 中立归属，否则各模块互相 include 会产生循环依赖。

## 模块职责与公开 API

### common/（头文件）
- `types.hpp` — `Variant`（全标量 + string）、`DataType` 枚举、`Timestamp`、
  `now()`、`dataTypeOf()`、`dataTypeName()`。`Variant` 备选类型顺序与 `DataType`
  严格对应，可由 `index()` 互推。
- `tag_value.hpp` — `TagValue{name, type, timestamp, value}`，构造时自动推导 type。
- `event.hpp` — `Event{source, category, message, severity, timestamp}` + `EventSeverity`。
- `stream.hpp` — `StreamFrame{source, type, timestamp, payload}` + `StreamType`。
  core 不解析流内容，仅作路由载体。

### logger/
`Logger::init(LoggerConfig)` / `get()` / `setLevel()` / `flush()`，宏 `IR_LOG_*`。
线程安全，控制台 + 滚动文件双 sink。core 其余模块只依赖此处，不直接用 spdlog。

### config/
`Config` 基于 nlohmann/json，点号路径访问：`get<T>("a.b.c", def)` / `tryGet<T>()` /
`has()` / `snapshot()`。读共享锁、加载独占锁。不抛异常，失败返回默认值/false。

### tag_engine/
`TagEngine` —— 16 分片 + 每片 `shared_mutex` 的并发 Tag 存储。
`write()`（返回是否变化）/ `writeBatch()` / `read()` / `exists()` / `remove()` /
`size()` / `snapshot()` / `setChangeCallback()`。值变化时触发回调。

### event_bus/
- `mpmc_queue.hpp` —— Vyukov 有界**无锁 MPMC 队列**，容量取 2 的幂，
  `tryEnqueue/tryDequeue` 单次尝试不阻塞。
- `EventBus` —— 生产者经无锁队列投递，单一 `jthread` 派发线程出队扇出。
  `counting_semaphore` 唤醒避免忙等。`publish()` / `subscribe(handler, Filter)` /
  `unsubscribe()` / `droppedCount()`。`Filter` 按最小严重级别 + 可选分类过滤。

### memory_store/
`MemoryStore` —— 16 分片通用 KV（`string → Variant`），与 Tag 体系区分（无时间戳语义）。
`set()` / `get()` / `getAs<T>()` / `exists()` / `erase()` / `size()` / `keys()` / `clear()`。

### scheduler/
`Scheduler` —— `jthread` + `stop_token` 可中断周期调度。
`addPeriodicTask(name, interval, task)` / `removeTask()` / `start()` / `stop()`。
任务表变动用版本号唤醒派发线程重算最近到期时间；到期任务在解锁状态下执行，
**耗时任务须自行卸载到其它线程**，避免阻塞后续调度。

### runtime_engine/
- `runtime_api.hpp` —— `RuntimeApi` 抽象接口：插件唯一可调用的入口
  `pushTag/pushEvent/pushStream`。跨 DLL 的 C-ABI 封送由 plugin-sdk 负责，
  core 内部以此抽象接口使用。
- `RuntimeEngine` —— **组合**（非继承）持有全部子系统，管理其生命周期。
  `init(Config)` / `start()` / `stop()`，实现 `RuntimeApi`，并提供
  `tags()/events()/store()/scheduler()` 内部访问（供运行时自身与测试，不对插件暴露）、
  `setStreamSink()`。

### plugin_host/
`PluginHost(RuntimeApi&)` —— 把 `RuntimeApi` 包装为插件可用的 C-ABI `IrPluginHostApi`
（见 `sdk/plugin-sdk`）。`abi()` 返回的指针经静态 thunk 把 `IrPluginTagValue/
IrPluginEvent/IrPluginStreamFrame`（纯 C 结构）转换为 core 类型后转发。这是宿主侧的封送层。

### plugin_manager/
`PluginManager(const IrPluginHostApi*)` —— 跨平台加载插件 DLL（Windows `LoadLibrary` /
POSIX `dlopen`）。`load()` 解析 `getPluginInfo`/`createPlugin`、校验 ABI 版本、
`createPlugin(host)` 并 `init()`；`startAll()`/`stopAll()`；析构时按逆序
`stop -> destroy -> 卸载库`。`destroy()` 在插件自身堆释放对象。

## 数据流

```
设备插件 --pushTag-->  RuntimeEngine --> TagEngine（存储 + 变更回调）
        --pushEvent--> RuntimeEngine --> EventBus（无锁队列 -> 派发线程 -> 订阅者）
        --pushStream-> RuntimeEngine --> StreamSink（转交 stream/ 模块）
```

Tag 与 Stream 是两套独立体系，互不混用。

## 模块依赖（CMake）

```
core_common (INTERFACE, 提供 include 根)
  ├── core_logger        -> spdlog
  ├── core_config        -> nlohmann_json
  ├── core_tag_engine    -> Threads
  ├── core_event_bus     -> Threads
  ├── core_memory_store  -> Threads
  ├── core_scheduler     -> Threads
  ├── core_runtime_engine -> 以上全部
  ├── core_plugin_host    -> core_runtime_engine + plugin_sdk
  └── core_plugin_manager -> plugin_sdk + core_logger (+ dl on POSIX)
IndustrialRuntime (exe) -> core_runtime_engine
plugin_sdk (INTERFACE, 来自 sdk/plugin-sdk) —— 宿主与插件共享的 ABI 契约
```

各模块为独立 STATIC 库，依赖边集中在 `core/CMakeLists.txt`。新增模块时保持低耦合，
新功能优先放对应模块，不要让 core_common 依赖任何子模块。

## 构建与测试

- 依赖经 vcpkg 清单模式提供（`vcpkg.json`：spdlog、nlohmann-json）。
- 中文 locale 下 MSVC 需 `/utf-8`（已在根 CMakeLists 配置），否则 UTF-8 注释被按
  GBK 解析导致编译错乱。
- 测试：CTest + `tests/test_util.hpp` 极简断言（零外部框架），每个测试独立可执行。
  运行：`ctest --test-dir cmake-build-debug --output-on-failure`。

## 本模块编码约束（叠加项目根 CLAUDE.md）

- Core 不得 include 任何设备/协议 SDK。
- 插件侧只能经 `RuntimeApi`，禁止暴露 TagEngine/EventBus/MemoryStore 内部。
- 内存用 `unique_ptr`/值语义，避免裸 `new/delete`。
- 线程用 `jthread` + `stop_token`，所有循环可退出，禁止 `detach()`。
- 必须线程安全、异常安全、跨平台；新代码配单元测试。
