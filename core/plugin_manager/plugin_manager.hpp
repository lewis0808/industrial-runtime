#pragma once

#include <string>
#include <vector>

#include "irplugin/plugin.hpp"
#include "irplugin/plugin_abi.h"

namespace core {

/// 插件管理器：跨平台加载插件 DLL 并管理其生命周期。
///
/// 加载流程：打开动态库 -> 解析 getPluginInfo/createPlugin -> 校验 ABI 版本 ->
/// createPlugin(host, path) -> init()。卸载流程：stop()（若已启动）-> destroy() ->
/// 关闭动态库。不跨边界传递 STL / 异常。线程不安全，由调用方串行使用。
class PluginManager {
  public:
    /// @param host 提供给插件的宿主 API（须在 PluginManager 存活期间有效）。
    explicit PluginManager(const IrPluginHostApi *host) noexcept;
    ~PluginManager();

    PluginManager(const PluginManager &) = delete;
    PluginManager &operator=(const PluginManager &) = delete;

    /// 加载并 init 一个插件 DLL。configPath 为该插件配置文件的完整路径，原样传给
    /// createPlugin（宿主不读取内容，由插件自行解析）。成功返回 true。
    bool load(const std::string &path, const std::string &configPath = "");

    /// 扫描 pluginDir 下所有动态库（*.dll / *.so / *.dylib）并逐个 load；每个插件的配置
    /// 路径取 configDir/<dll basename>.json。pluginDir 不存在视为无插件，返回加载成功数。
    std::size_t loadDirectory(const std::string &pluginDir, const std::string &configDir);

    /// 对所有已加载插件调用 start()。返回是否全部成功。
    bool startAll();

    /// 对所有已加载插件调用 stop()。返回是否全部成功。
    bool stopAll();

    /// 已加载插件数量。
    [[nodiscard]] std::size_t count() const noexcept { return plugins_.size(); }

    /// 返回第 index 个插件的元信息（index 越界返回空 info）。
    [[nodiscard]] IrPluginInfo infoAt(std::size_t index) const;

  private:
    struct Loaded {
        void *handle; ///< 平台相关的动态库句柄
        IrPluginInfo info;
        irplugin::IPlugin *plugin; ///< 由插件 DLL 分配，须经 destroy() 释放
        bool started;
    };

    void unloadAll() noexcept;

    const IrPluginHostApi *host_;
    std::vector<Loaded> plugins_;
};

} // namespace core
