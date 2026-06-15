# irplugin — 设备插件开发 SDK

面向 **Industrial Runtime** 的设备插件 SDK。**纯头文件、零外部依赖、不引用 runtime 内部**——
插件只通过稳定的 **C ABI** 与 runtime 通信，用任意工具链在**独立项目**里开发。

- `example/irplugin/plugin_abi.h` —— 纯 C ABI 契约（宿主 ↔ 插件唯一二进制边界），含生命周期
  C 函数指针 vtable `IrPluginInstance`。
- `example/irplugin/plugin.hpp` —— C++ 封装：`IPlugin` 接口 + `Host`（pushTag/pushEvent/pushStream/onWrite）
  + `makeInstance`（把 `IPlugin` 封装进 C vtable）。

> 当前 ABI：`IRPLUGIN_ABI_VERSION = 3`（runtime 接受版本在 `[3, 3]` 区间的插件）。
> **v3 起生命周期改为纯 C 函数指针 vtable**：`createPlugin` 不再返回 C++ 对象，故宿主与插件
> **无需同一套 C++ ABI**——插件可用任意语言/编译器实现（C++ 作者用下方 `makeInstance` 一行封装即可）。

---

## 用法：直接复制头文件

插件项目**与本 runtime 仓库无关**，标准做法是把这两个头拷进你的插件工程即可——
无需安装、无需 `find_package`、不依赖本项目任何内部 target。

把 `example/irplugin/` 下的 `plugin_abi.h` 与 `plugin.hpp` 整个 `irplugin/` 目录拷过去
（`#include "irplugin/plugin.hpp"` 这个前缀是固定契约，**要保留 `irplugin/` 这层目录**）。

### 推荐的插件工程结构

```
my-plugin/
├── CMakeLists.txt
├── vcpkg.json            # 你自己的第三方依赖（如设备 SDK）
├── irplugin/             # ← 从本 SDK 拷来的两个头（保留 irplugin/ 这层）
│   ├── plugin_abi.h
│   └── plugin.hpp
└── src/
    └── my_plugin.cpp
```

### CMake 写法

```cmake
cmake_minimum_required(VERSION 3.20)
project(my-plugin CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 把内置的 SDK 头包装成 irplugin::plugin_sdk。
# include 根设为项目根，使 #include "irplugin/..." 的前缀生效。
add_library(irplugin_sdk INTERFACE)
add_library(irplugin::plugin_sdk ALIAS irplugin_sdk)
target_include_directories(irplugin_sdk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(irplugin_sdk INTERFACE cxx_std_20)

# 插件本体：动态库（MODULE）。文件名任意。
add_library(my_plugin MODULE src/my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE irplugin::plugin_sdk)
set_target_properties(my_plugin PROPERTIES PREFIX "")   # 输出 my_plugin.dll / .so
```

### 写一个插件（最小骨架）

```cpp
#include "irplugin/plugin.hpp"

namespace {
class MyPlugin final : public irplugin::IPlugin {
  public:
    explicit MyPlugin(const IrPluginHostApi *host) noexcept : host_(host) {}
    bool init() override { return host_.valid(); }
    bool start() override { host_.pushTag("my/tag", 42.0); return true; }
    bool stop() override { return true; }
    bool destroy() override { delete this; return true; }
  private:
    irplugin::Host host_;
};
} // namespace

IRPLUGIN_EXPORT IrPluginInfo getPluginInfo() {
    return IrPluginInfo{IRPLUGIN_ABI_VERSION, "my", "My Plugin", "1.0.0"};
}
// createPlugin(host, config_path, out)：把插件封进 C vtable 填入 out，成功返回 1。
// config_path 为该插件配置文件完整路径（runtime 透传，无需配置可忽略）。
IRPLUGIN_EXPORT int createPlugin(const IrPluginHostApi *host, const char *config_path,
                                 IrPluginInstance *out) {
    return irplugin::makeInstance(new (std::nothrow) MyPlugin(host), out);
}
```

> 用任意语言写插件时无需 `IPlugin`/`makeInstance`：只要导出 `getPluginInfo` 与 `createPlugin`，
> 在 `createPlugin` 里自行填好 `IrPluginInstance` 的 `self` 与四个 C 函数指针即可（见
> `plugin_abi.h` 中 `IrPluginInstance` 注释）。

---

## 部署

把构建出的动态库丢进 runtime **可执行文件同级的 `plugins/`** 目录即被自动发现加载
（导出 `getPluginInfo`/`createPlugin` 即可，文件名任意）。

```
<runtime-exe>/
├── plugins/
│   ├── my_plugin.dll       # 你的插件
│   └── *.dll               # 插件的运行时依赖（若有），需与插件同目录
└── config/
    └── my_plugin.json      # 配置：文件名 = 插件 dll 的 basename（runtime 经 config_path 传入）
```

- 配置与插件**分目录**：插件放 `plugins/`，配置放 `config/`。
- 插件如有依赖 dll（如设备厂商 SDK），与插件 dll **同目录**放（runtime 用
  `LOAD_WITH_ALTERED_SEARCH_PATH` 解析）；若想 `plugins/` 里只有纯插件、避免依赖 dll 被
  自动发现误扫，可把依赖**静态链接进插件**，或把依赖 dll 放 runtime 的 exe 目录。

---

## 约定

- 跨 DLL 边界**只走 C ABI**：不跨边界传 STL 对象 / 抛异常 / 导出 C++ 类；**生命周期亦为纯 C
  函数指针 vtable（`IrPluginInstance`）**，宿主与插件无需同一 C++ ABI。
- 动态库**文件名任意**；插件 id / 名来自 `getPluginInfo`。
- `createPlugin(host, config_path, out)`：第二参是 runtime 透传的**配置文件完整路径**（runtime
  不读取内容，由插件自行解析、按需热扫描）；第三参 `out` 由插件填好实例 vtable，成功返回 1。
- 升级 SDK：覆盖这两个头，核对 `IRPLUGIN_ABI_VERSION` 与 `createPlugin` 签名是否变化。

## 示例

- `example/` —— **SDK 自带的最小参考插件**（无第三方依赖，可单独 `cmake` 构建），演示 `IPlugin` /
  `Host` / `onWrite` 的写法。
- `industrial-runtime-s7` —— **完整设备插件工程**（S7/snap7，含可复用 `libs7` + 配置解析 +
  热扫描 + 多 PLC），就是按本文「拷头」方式组织的。
