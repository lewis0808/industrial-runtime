#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "irplugin/plugin_abi.h"
#include "plugin_host/plugin_host.hpp"

namespace core {

/// 插件管理器：跨平台加载插件 DLL 并管理其生命周期，支持运行期按 id 热卸载 / reload。
///
/// 加载流程：打开动态库 -> 解析 getPluginInfo/createPlugin -> 校验 ABI 版本 ->
/// createPlugin(host, path) -> init()。卸载流程：撤销该插件写回并排空在途调用 ->
/// stop()（若已启动）-> destroy() -> 关闭动态库。不跨边界传递 STL / 异常。
///
/// **线程安全**：内部 mutex 串行化所有管理操作（load/unload/reload/startAll/...），
/// 故运行期可由 IRSP 服务线程触发卸载，与采集主线程并行。写回排空依赖 PluginHost
/// 的引用计数：卸载前先撤销写回归属，等待在途 write() 结束，再 destroy/卸库。
class PluginManager {
  public:
    /// 单个插件的对外描述（id/name/version 已拷出，不指向 DLL 内存）。
    struct PluginDesc {
        std::string id;
        std::string name;
        std::string version;
        bool started;
    };

    /// @param host 宿主接口与写回归属控制（须在 PluginManager 存活期间有效）。
    explicit PluginManager(PluginHost &host) noexcept;
    ~PluginManager();

    PluginManager(const PluginManager &) = delete;
    PluginManager &operator=(const PluginManager &) = delete;

    /// 加载并 init 一个插件 DLL。configPath 为该插件配置文件的完整路径，原样传给
    /// createPlugin（宿主不读取内容，由插件自行解析）。成功返回 true。
    bool load(const std::string &path, const std::string &configPath = "");

    /// 扫描 pluginDir 下所有动态库（*.dll / *.so / *.dylib）并逐个 load；每个插件的配置
    /// 路径取 configDir/<dll basename>.json。pluginDir 不存在视为无插件，返回加载成功数。
    std::size_t loadDirectory(const std::string &pluginDir, const std::string &configDir);

    /// 对所有未启动插件调用 start()。返回是否全部成功。
    bool startAll();

    /// 对所有已启动插件调用 stop()。返回是否全部成功。
    bool stopAll();

    /// 按 id 热卸载一个插件：撤销其写回并排空在途调用 -> stop（若已启动）-> destroy ->
    /// 卸库 -> 从表移除。找不到该 id 返回 false。
    bool unload(const std::string &id);

    /// 按 id 重载一个插件：以原 path/configPath 卸载后重新加载；若原先已 start 则重新 start。
    /// 找不到该 id 或重新加载失败返回 false（失败时该插件已被卸载，不会残留旧实例）。
    bool reload(const std::string &id);

    /// 重新扫描插件目录（loadDirectory 时记下的），加载并 start **尚未加载**的插件
    /// （按路径去重，已加载的跳过）。运行期把启动时的自动发现再跑一遍，用于把新放入或
    /// 卸载后想装回的插件接入。返回本次新加载的数量。未先 loadDirectory 则返回 0。
    std::size_t scan();

    /// 已加载插件数量。
    [[nodiscard]] std::size_t count() const;

    /// 列出全部已加载插件描述（用于管理/查询）。
    [[nodiscard]] std::vector<PluginDesc> list() const;

  private:
    struct Loaded {
        void *handle; ///< 平台相关的动态库句柄
        IrPluginInfo info;
        IrPluginInstance instance; ///< C vtable（宿主按值持有）；instance.self 由插件分配，经 destroy 释放
        bool started;
        std::string path;       ///< DLL 路径（reload 用）
        std::string configPath; ///< 配置路径（reload 用）
        PluginHost::OwnerId owner; ///< 写回归属，卸载时按此撤销 + 排空
    };

    // 以下 *Locked 助手假定已持 mutex_。
    bool loadLocked(const std::string &path, const std::string &configPath);
    void unloadLocked(std::size_t index) noexcept;
    bool startLocked(Loaded &p);
    [[nodiscard]] std::size_t findIndexLocked(const std::string &id) const;
    [[nodiscard]] bool isLoadedPathLocked(const std::string &path) const;
    /// 扫描 pluginDir_ 下动态库，加载未加载者（按路径去重）；startNew 时一并 start。返回新加载数。
    std::size_t discoverLocked(bool startNew);

    PluginHost *host_;
    mutable std::mutex mutex_;
    std::vector<Loaded> plugins_;
    std::string pluginDir_; ///< loadDirectory 记下的插件目录（供 scan 复用）
    std::string configDir_; ///< loadDirectory 记下的配置目录
};

} // namespace core
