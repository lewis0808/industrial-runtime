# 插件系统 — ABI 契约 + 宿主封送 + 生命周期

涉及三件：`irplugin/plugin_abi.h`（纯 C 契约）、`irplugin/plugin.hpp`（插件作者用的 C++ 头）、
`plugin_host/`（宿主侧封送）、`plugin_manager/`（DLL 加载与生命周期）。

> 插件 ABI 与对外协议 IRSP 是**两个正交的层**：ABI 是设备数据「入口」，IRSP 是应用「出口」，
> 互不共用类型。

## 1. ABI 契约（irplugin/plugin_abi.h，纯 C）

宿主与插件 DLL 之间**唯一的二进制边界**。设计目标：ABI 稳定，**不跨 DLL 传 STL/异常/C++ 类**。

- `IRPLUGIN_ABI_VERSION = 3`，`IRPLUGIN_ABI_MIN_VERSION = 3`。宿主接受 `abi_version` 落在
  `[MIN, VERSION]` 区间的插件；结构体**只在末尾追加**字段 → 区间内旧插件只读已知前缀，向后兼容。
  （v2 起 `IrPluginHostApi` 追加 `register_writer`；**v3 起生命周期改 C 函数指针 vtable**，
  `createPlugin` 签名破坏性变更，故设最低版本拒绝 < 3 的旧插件。）
- 数据结构：`IrPluginVariant`（tagged union）/ `IrPluginTagValue` / `IrPluginEvent` /
  `IrPluginStreamFrame`；枚举取值与 `core::DataType/EventSeverity/StreamType` **严格同序**。
- 字符串 `IrPluginString{data,len}`：非拥有、不要求 `\0`，宿主**在回调内同步拷贝**，返回后即失效。
- 宿主 API `IrPluginHostApi`：`ctx + push_tag/push_event/push_stream + register_writer`，
  全是 C 函数指针，`ctx` 插件原样回传。
- **生命周期 vtable `IrPluginInstance`**：`self` + `init/start/stop/destroy` 四个 C 函数指针
  （以 `self` 为首参）。宿主按值持有此 POD，只调 C 函数指针——**不再跨 DLL 调 C++ 虚函数**，
  故宿主与插件无需同一 C++ ABI，插件可用任意语言/编译器实现。`self` 归插件所有，经 `destroy` 释放。
- 导出符号：`getPluginInfo`（`IrPluginGetInfoFn`）、`createPlugin`（`IrPluginCreateFn`）。

## 2. 插件作者侧（irplugin/plugin.hpp，C++ 便利层）

- `class IPlugin`：纯抽象，`init/start/stop/destroy`。生命周期
  `createPlugin → init → start → … → stop → destroy`；`destroy()` 通常 `delete this`（插件自身堆释放）。
  **它只是 C++ 作者的便利基类**——`makeInstance` 生成的蹦床把虚调用全部留在插件 DLL 内，
  故 `IPlugin` 的 vtable 不跨 DLL 边界（宿主只见 C 函数指针）。
- `makeInstance(IPlugin*, IrPluginInstance* out)`：把已构造的 `IPlugin` 封进 C vtable 填入 `out`
  （蹦床 `detail::abiInit/Start/Stop/Destroy` → 转调虚函数）。`createPlugin` 一行即用。
- `class Host`：把 C++ 值封送进 C 结构调宿主 API。`pushTag`（算术/字符串重载）、`pushEvent`、
  `pushStream`、`onWrite(prefix, cb)`（经 `register_writer` 注册写回，蹦床以 `this` 为 ctx
  → **注册后勿拷贝/移动 Host**）。
- `createPlugin(host, config_path, out)`：成功返回 1 并填充 `*out`（见 `makeInstance`）。
  `config_path` 为该插件配置文件**完整路径**（宿主算出 `<exe>/config/<dll basename>.json` 透传，
  **不读取内容**），插件自行解析、按需热扫描，缺失则用内置默认。
  非 C++ 语言可绕过 `IPlugin`/`makeInstance`，直接在 `createPlugin` 里填好 `IrPluginInstance`。

## 3. 宿主封送（plugin_host/）

`PluginHost(RuntimeApi&)` 把 `RuntimeApi` 包装成 C-ABI `IrPluginHostApi`：

- 静态 thunk（`pushTagThunk` 等）以 `this` 为 `ctx`，把 C 结构 → core 类型后转发给 `RuntimeApi`。
  `timestamp_ns == 0` 由宿主填当前时间；字符串/payload 同步拷贝。thunk 全部 `noexcept` 且包
  `try/catch`：宿主侧异常（如 `bad_alloc`）不得逃逸回插件（跨 C-ABI 即 UB），按丢弃（返回 0）处理。
- **写回路由** `write(TagValue)`：遍历 `writers_`，按 **topic 前缀**匹配，命中多个时取**最长前缀**的插件
  （与注册顺序无关），封送回 C 结构调插件的 `IrPluginWriteFn`。该调用进入插件代码，故包 `try/catch`，
  插件抛异常按未受理（返回 false）处理。
- **并发安全**：`writers_` 由 `shared_mutex` 保护——`register_writer`（启动期/运行期均可）取独占锁并对
  **完全相同的 prefix 去重告警**（`a/` 与 `a/b/` 属正常分层不算冲突）；`write` 取共享锁，**仅在锁内选出
  处理器并拷出，出锁后再调用插件代码**，不持锁执行外部代码（避免长持锁 / 潜在死锁），支持运行期动态注册
  与并发写回。

## 4. 生命周期管理（plugin_manager/）

`PluginManager(const IrPluginHostApi*)`：跨平台 DLL 加载（Windows `LoadLibraryEx` +
`LOAD_WITH_ALTERED_SEARCH_PATH` 解析插件同目录依赖，如 `snap7.dll`；POSIX `dlopen`）。

- `load(path, configPath)`：打开库 → 解析 `getPluginInfo/createPlugin` → 校验 ABI 版本（区间
  `[MIN, VERSION]`）→ `createPlugin(host, configPath, &instance)` 填充 C vtable → **校验
  `self` 与四个函数指针非空**（不全则视为插件实现错误，无法安全 destroy，仅关库）→ `init()`。
  宿主按值持有 `instance`；任一步失败即清理并返回 false。
  **无导出符号的动态库（插件依赖库）静默跳过**——支持自动发现目录里混放依赖。
  对插件代码的每次调用（`getPluginInfo/createPlugin/init/start/stop/destroy`）均经 `guardedCall`
  包 `try/catch`：插件抛异常跨 DLL 即 UB，宿主统一拦截、记日志并按失败处理；`unloadAll` 为
  `noexcept`，其中的 `stop/destroy` 若漏拦异常会直接 `std::terminate`，故同样兜底。
- `loadDirectory(pluginDir, configDir)`：扫 `*.dll/.so/.dylib` 逐个 load，配置路径取
  `configDir/<basename>.json`。
- `startAll()/stopAll()`；析构按**逆序** `撤销写回+排空 → stop → destroy → 关库`。
- **线程安全**：内部 `mutex` 串行化所有管理操作（load/unload/reload/startAll/...），
  故运行期可由 IRSP 服务线程触发卸载，与采集主线程并行。

### 4.1 按 id 热卸载 / reload（owner 归属 + 引用计数排空）

`unload(id)` / `reload(id)` 支持运行期按插件 id 热卸载与重载。难点是**写回路径的并发安全**：
插件经 `register_writer` 注册的 handler 是指向其 DLL 的函数指针、`pluginCtx` 指向其内存，
若卸载时不先撤销并确保无在途调用，`write()` 会调用到已 `FreeLibrary` 的代码（use-after-free）。

解法分两层：

1. **owner 归属**（PluginHost）：每个加载的插件分配一个 `OwnerId`。`PluginManager` 在调用插件
   `init/start` 前后 `setActiveOwner(owner)`/`setActiveOwner(0)`，使插件在生命周期回调内注册的
   写回自动归于该 owner（其外注册归 owner 0「无归属」，永不撤销）。
2. **引用计数排空**（PluginHost）：每个 owner 持一个 `inflight` 原子计数。`write()` 在**共享锁内**
   选出最长前缀处理器并**自增其 owner 的 inflight**，出锁后再调用插件代码，调用结束自减。
   卸载时 `retireOwner` 取**独占锁**摘除该 owner 的全部写回——与 `write()` 的选取互斥，故
   此后不会再有新的自增；`waitQuiescent` 自旋等待 `inflight` 归零（在途调用结束），才放行
   `stop/destroy/卸库`。owner 状态以 `shared_ptr` 持有，`write()` 出锁调用期间即便 `removeOwner`
   也不悬空。

卸载流程（`unloadLocked`）：`retireOwner → waitQuiescent → stop（若已启动）→ destroy →
removeOwner → 卸库 → 从表移除`。`reload` = 记下原 `path/config/started` → `unload` →
以同参 `load` → 原先 `started` 则重新 `start`（重载失败时旧实例已卸载，不残留）。

控制面入口：**独立的本机 admin 通道**（Windows 命名管道 / POSIX AF_UNIX）上的
`PLUGIN LIST/UNLOAD/RELOAD`，**与 IRSP 数据面解耦**（控制类有副作用操作不污染数据总线，
README §7 原则）。协议见 [admin/README.md](../../../admin/README.md)，实现见 `admin/` 模块
（命令处理纯函数 `handleAdminCommand` + 命名管道/AF_UNIX 传输）。单测 `test_plugin_host`
覆盖 owner 撤销与并发排空，`test_admin_command` 覆盖控制命令；DLL 级集成由独立插件工程覆盖。

## 待改善

| 项 | 说明 | 优先级/方向 |
|----|------|-------------|
| ~~生命周期仍是 C++ vtable~~（已解决） | ~~`createPlugin` 返回 `IPlugin*`，`init/start/stop/destroy` 是跨 DLL 的虚函数调用 → 要求宿主与插件同一 C++ ABI。~~ | ✅ v3 起生命周期改 C 函数指针 vtable `IrPluginInstance`：`createPlugin(host, cfg, out)` 填充 `self + init/start/stop/destroy`，宿主只调 C 指针，无需同一 C++ ABI。C++ 作者用 `makeInstance` 一行封装（蹦床留在插件 DLL 内）。 |
| ~~跨边界异常无防护~~（已解决） | 插件 `createPlugin/init/start/stop/destroy/write` 跨 DLL 抛异常 → UB。 | ✅ 宿主侧 `guardedCall`/thunk `noexcept` + `try/catch` 双向隔离；单测 `test_plugin_host` 覆盖写回与 push 边界。 |
| ~~写回路由是线性首匹配~~（已解决） | ~~`write` O(N) 扫前缀、首个匹配胜出，无最长前缀、无冲突检测。~~ | ✅ 改最长前缀匹配（与注册顺序无关）+ 同前缀去重告警。仍 O(N) 扫描，但 writers 数量级为插件数，无需索引。 |
| ~~writers_ 无同步~~（已解决） | ~~注册在 init（启动期单线程），但 `write` 跑在 IRSP 服务线程；运行期动态注册则竞态。~~ | ✅ `shared_mutex` 保护：注册独占、写回共享；写回锁内选出处理器、出锁再调用插件，支持运行期动态注册。 |
| ~~无热插拔/卸载~~（已解决） | ~~无按 id 卸载、reload、热替换；`writers_` 只增不减。~~ | ✅ `unload(id)/reload(id)`：owner 归属 + 引用计数排空保证写回路径无 use-after-free；`PluginManager` 线程安全；独立 admin 通道 `PLUGIN` 控制命令接入。见 §4.1。 |
| **进程内无隔离** | 插件崩溃（段错误 / `abort` / 死循环）直接拖垮运行时，C-ABI 异常隔离也挡不住非 C++ 异常的硬故障。 | 关键场景走**进程外插件**（子进程 + IPC），崩溃/卸载 = 杀进程，天然隔离。方案探索见 [plugin-out-of-process.md](plugin-out-of-process.md)。 |
