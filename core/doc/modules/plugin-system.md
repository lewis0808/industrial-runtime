# 插件系统 — ABI 契约 + 宿主封送 + 生命周期

涉及三件：`irplugin/plugin_abi.h`（纯 C 契约）、`irplugin/plugin.hpp`（插件作者用的 C++ 头）、
`plugin_host/`（宿主侧封送）、`plugin_manager/`（DLL 加载与生命周期）。

> 插件 ABI 与对外协议 IRSP 是**两个正交的层**：ABI 是设备数据「入口」，IRSP 是应用「出口」，
> 互不共用类型。

## 1. ABI 契约（irplugin/plugin_abi.h，纯 C）

宿主与插件 DLL 之间**唯一的二进制边界**。设计目标：ABI 稳定，**不跨 DLL 传 STL/异常/C++ 类**。

- `IRPLUGIN_ABI_VERSION = 2`。宿主接受 `abi_version <= 自身` 的插件；结构体**只在末尾追加**字段
  → 旧插件只读已知前缀，向后兼容。（v2 起 `IrPluginHostApi` 追加 `register_writer`。）
- 数据结构：`IrPluginVariant`（tagged union）/ `IrPluginTagValue` / `IrPluginEvent` /
  `IrPluginStreamFrame`；枚举取值与 `core::DataType/EventSeverity/StreamType` **严格同序**。
- 字符串 `IrPluginString{data,len}`：非拥有、不要求 `\0`，宿主**在回调内同步拷贝**，返回后即失效。
- 宿主 API `IrPluginHostApi`：`ctx + push_tag/push_event/push_stream + register_writer`，
  全是 C 函数指针，`ctx` 插件原样回传。
- 导出符号：`getPluginInfo`、`createPlugin`。

## 2. 插件作者侧（irplugin/plugin.hpp，C++ 便利层）

- `class IPlugin`：纯抽象，`init/start/stop/destroy`。生命周期
  `createPlugin → init → start → … → stop → destroy`；`destroy()` 通常 `delete this`（插件自身堆释放）。
- `class Host`：把 C++ 值封送进 C 结构调宿主 API。`pushTag`（算术/字符串重载）、`pushEvent`、
  `pushStream`、`onWrite(prefix, cb)`（经 `register_writer` 注册写回，蹦床以 `this` 为 ctx
  → **注册后勿拷贝/移动 Host**）。
- `createPlugin(host, config_path)`：第二参为该插件配置文件**完整路径**（宿主算出
  `<exe>/config/<dll basename>.json` 透传，**不读取内容**），插件自行解析、按需热扫描，缺失则用内置默认。

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

- `load(path, configPath)`：打开库 → 解析 `getPluginInfo/createPlugin` → 校验 ABI 版本 →
  `createPlugin(host, configPath)` → `init()`。任一步失败即清理并返回 false。
  **无导出符号的动态库（插件依赖库）静默跳过**——支持自动发现目录里混放依赖。
  对插件代码的每次调用（`getPluginInfo/createPlugin/init/start/stop/destroy`）均经 `guardedCall`
  包 `try/catch`：插件抛异常跨 DLL 即 UB，宿主统一拦截、记日志并按失败处理；`unloadAll` 为
  `noexcept`，其中的 `stop/destroy` 若漏拦异常会直接 `std::terminate`，故同样兜底。
- `loadDirectory(pluginDir, configDir)`：扫 `*.dll/.so/.dylib` 逐个 load，配置路径取
  `configDir/<basename>.json`。
- `startAll()/stopAll()`；析构 `unloadAll()` 按**逆序** `stop → destroy → 关库`。
- **线程不安全**：由调用方串行使用（`main` 在启动期单线程加载）。

## 待改善

| 项 | 说明 | 优先级/方向 |
|----|------|-------------|
| **生命周期仍是 C++ vtable** | `createPlugin` 返回 `IPlugin*`，`init/start/stop/destroy` 是跨 DLL 的**虚函数调用** → 要求宿主与插件**同一 C++ ABI（编译器/运行时）**。数据面虽是纯 C ABI，但生命周期这层尚未 C 化。 | **高（架构级）**：把生命周期改为 `IrPluginInfo` 内的 C 函数指针 vtable，才能真正「任意语言/编译器」写插件。 |
| ~~跨边界异常无防护~~（已解决） | 插件 `createPlugin/init/start/stop/destroy/write` 跨 DLL 抛异常 → UB。 | ✅ 宿主侧 `guardedCall`/thunk `noexcept` + `try/catch` 双向隔离；单测 `test_plugin_host` 覆盖写回与 push 边界。 |
| ~~写回路由是线性首匹配~~（已解决） | ~~`write` O(N) 扫前缀、首个匹配胜出，无最长前缀、无冲突检测。~~ | ✅ 改最长前缀匹配（与注册顺序无关）+ 同前缀去重告警。仍 O(N) 扫描，但 writers 数量级为插件数，无需索引。 |
| ~~writers_ 无同步~~（已解决） | ~~注册在 init（启动期单线程），但 `write` 跑在 IRSP 服务线程；运行期动态注册则竞态。~~ | ✅ `shared_mutex` 保护：注册独占、写回共享；写回锁内选出处理器、出锁再调用插件，支持运行期动态注册。 |
| **无热插拔/卸载** | 无按 id 卸载、reload、热替换；`writers_` 只增不减。 | 视运维需求补 unload/reload。 |
| **进程内无隔离** | 插件崩溃直接拖垮运行时。 | 关键场景可考虑进程外插件（IPC/共享内存）方案。 |
