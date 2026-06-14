#include "plugin_manager/plugin_manager.hpp"

#include <exception>
#include <filesystem>
#include <type_traits>
#include <utility>

#include "logger/logger.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace core {

namespace {

void *openLibrary(const std::string &path) {
#if defined(_WIN32)
    // LOAD_WITH_ALTERED_SEARCH_PATH：以 dll 所在目录（path 为绝对路径）作为依赖搜索起点，
    // 使插件能找到与其同目录的依赖库（如 s7_plugin.dll 旁的 snap7.dll）。
    return ::LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void *handle) {
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

void *findSymbol(void *handle, const char *name) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

/// 跨 DLL 调用插件代码的统一异常兜底。ABI 约定不跨边界传异常；插件若抛出即 UB，
/// 宿主在此拦截、记日志并返回 fallback，保证单个插件故障不波及运行时（含 noexcept
/// 的卸载路径，异常逃逸会直接 std::terminate）。
template <typename Fn>
std::invoke_result_t<Fn> guardedCall(const char *action, const char *id,
                                     std::invoke_result_t<Fn> fallback, Fn &&fn) noexcept {
    try {
        return std::forward<Fn>(fn)();
    } catch (const std::exception &e) {
        IR_LOG_ERROR("插件 {} 抛出异常({}): {}", action, id ? id : "?", e.what());
    } catch (...) {
        IR_LOG_ERROR("插件 {} 抛出未知异常({})", action, id ? id : "?");
    }
    return fallback;
}

} // namespace

PluginManager::PluginManager(PluginHost &host) noexcept : host_(&host) {}

PluginManager::~PluginManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 逆序卸载，保证依赖关系（后加载的先卸）。
    while (!plugins_.empty()) {
        unloadLocked(plugins_.size() - 1);
    }
}

bool PluginManager::load(const std::string &path, const std::string &configPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loadLocked(path, configPath);
}

bool PluginManager::loadLocked(const std::string &path, const std::string &configPath) {
    void *handle = openLibrary(path);
    if (handle == nullptr) {
        IR_LOG_ERROR("插件加载失败，无法打开动态库: {}", path);
        return false;
    }

    auto getInfo =
        reinterpret_cast<IrPluginGetInfoFn>(findSymbol(handle, IRPLUGIN_SYM_GET_PLUGIN_INFO));
    auto createPlugin =
        reinterpret_cast<IrPluginCreateFn>(findSymbol(handle, IRPLUGIN_SYM_CREATE_PLUGIN));
    if (getInfo == nullptr || createPlugin == nullptr) {
        // 自动发现目录里可能混有非插件动态库（如插件的依赖库），跳过属正常情况。
        IR_LOG_INFO("跳过非插件动态库（无 {}/{}）: {}", IRPLUGIN_SYM_GET_PLUGIN_INFO,
                    IRPLUGIN_SYM_CREATE_PLUGIN, path);
        closeLibrary(handle);
        return false;
    }

    IrPluginInfo info{};
    try {
        info = getInfo();
    } catch (...) {
        IR_LOG_ERROR("插件 getPluginInfo 抛出异常，跳过: {}", path);
        closeLibrary(handle);
        return false;
    }
    // 接受 [MIN, VERSION] 区间：过新无法理解；过旧（< v3）createPlugin 签名不兼容，强行
    // 调用即 UB，必须拒绝。HostApi 仅末尾追加，故区间内宿主向后兼容。
    if (info.abi_version > IRPLUGIN_ABI_VERSION || info.abi_version < IRPLUGIN_ABI_MIN_VERSION) {
        IR_LOG_ERROR("插件 ABI 版本不兼容（插件 {}，宿主支持 [{}, {}]）: {}", info.abi_version,
                     IRPLUGIN_ABI_MIN_VERSION, IRPLUGIN_ABI_VERSION, path);
        closeLibrary(handle);
        return false;
    }

    // 分配写回归属并设为 active：插件在 init/start 内经 register_writer 注册的写回自动归于此
    // owner，卸载时按 owner 撤销并排空在途调用。
    const PluginHost::OwnerId owner = host_->createOwner();
    host_->setActiveOwner(owner);

    // 把配置文件路径原样传给插件（宿主不读取内容，由插件自行解析）。createPlugin 填充 C vtable：
    // 成功返回 1，宿主按值持有 instance；仅 instance.self 归插件所有，经 destroy 释放。
    IrPluginInstance instance{};
    const bool created = guardedCall("createPlugin", info.id, false, [&] {
        return createPlugin(host_->abi(), configPath.c_str(), &instance) != 0;
    });
    if (!created || instance.self == nullptr || instance.init == nullptr ||
        instance.start == nullptr || instance.stop == nullptr || instance.destroy == nullptr) {
        // vtable 不全则无法安全 destroy，只能关库（视为插件实现错误）。
        IR_LOG_ERROR("插件 createPlugin 失败或未填满实例 vtable: {}", path);
        host_->setActiveOwner(0);
        host_->removeOwner(owner);
        closeLibrary(handle);
        return false;
    }

    if (!guardedCall("init", info.id, false, [&] { return instance.init(instance.self) != 0; })) {
        IR_LOG_ERROR("插件 init 失败: {}", info.id ? info.id : "?");
        guardedCall("destroy", info.id, false,
                    [&] { return instance.destroy(instance.self) != 0; });
        host_->setActiveOwner(0);
        host_->retireOwner(owner); // init 期可能已注册部分写回，一并撤销
        host_->waitQuiescent(owner);
        host_->removeOwner(owner);
        closeLibrary(handle);
        return false;
    }

    host_->setActiveOwner(0);
    plugins_.push_back(Loaded{handle, info, instance, false, path, configPath, owner});
    IR_LOG_INFO("插件已加载: {} ({})", info.name ? info.name : "?",
                info.version ? info.version : "?");
    return true;
}

std::size_t PluginManager::loadDirectory(const std::string &pluginDir,
                                         const std::string &configDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    pluginDir_ = pluginDir; // 记下目录供 scan 复用
    configDir_ = configDir;
    return discoverLocked(false); // 启动期只加载，由调用方随后 startAll
}

std::size_t PluginManager::scan() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pluginDir_.empty()) {
        IR_LOG_WARN("scan：尚未设置插件目录（未调用 loadDirectory）");
        return 0;
    }
    return discoverLocked(true); // 运行期发现：新插件随即 start
}

bool PluginManager::isLoadedPathLocked(const std::string &path) const {
    for (const auto &p : plugins_) {
        if (p.path == path) {
            return true;
        }
    }
    return false;
}

std::size_t PluginManager::discoverLocked(bool startNew) {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const char *ext = ".dll";
#elif defined(__APPLE__)
    const char *ext = ".dylib";
#else
    const char *ext = ".so";
#endif
    std::error_code ec;
    if (!fs::is_directory(pluginDir_, ec)) {
        IR_LOG_INFO("插件目录不存在，跳过自动发现: {}", pluginDir_);
        return 0;
    }
    std::size_t loaded = 0;
    for (const auto &entry : fs::directory_iterator(pluginDir_, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ext) {
            continue;
        }
        const auto path = entry.path().string();
        if (isLoadedPathLocked(path)) {
            continue; // 已加载，按路径去重跳过
        }
        // 配置文件路径：configDir/<dll basename>.json（与 plugins 目录分离）。
        const auto configPath = (fs::path(configDir_) / entry.path().stem()).string() + ".json";
        if (loadLocked(path, configPath)) {
            if (startNew) {
                startLocked(plugins_.back());
            }
            ++loaded;
        }
    }
    return loaded;
}

bool PluginManager::startLocked(Loaded &p) {
    if (p.started) {
        return true;
    }
    // start 期插件也可能注册写回，归于其 owner。
    host_->setActiveOwner(p.owner);
    const bool ok = guardedCall("start", p.info.id, false,
                                [&] { return p.instance.start(p.instance.self) != 0; });
    host_->setActiveOwner(0);
    if (ok) {
        p.started = true;
    } else {
        IR_LOG_ERROR("插件 start 失败: {}", p.info.id ? p.info.id : "?");
    }
    return ok;
}

bool PluginManager::startAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool ok = true;
    for (auto &p : plugins_) {
        if (!startLocked(p)) {
            ok = false;
        }
    }
    return ok;
}

bool PluginManager::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool ok = true;
    for (auto &p : plugins_) {
        if (p.started) {
            if (!guardedCall("stop", p.info.id, false,
                             [&] { return p.instance.stop(p.instance.self) != 0; })) {
                ok = false;
                IR_LOG_ERROR("插件 stop 失败: {}", p.info.id ? p.info.id : "?");
            }
            p.started = false;
        }
    }
    return ok;
}

std::size_t PluginManager::findIndexLocked(const std::string &id) const {
    for (std::size_t i = 0; i < plugins_.size(); ++i) {
        const char *pid = plugins_[i].info.id;
        if (pid != nullptr && id == pid) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

void PluginManager::unloadLocked(std::size_t index) noexcept {
    Loaded &p = plugins_[index];
    // 1) 撤销该插件写回并排空在途调用——此后 write() 不会再进入它，已在途的调用已结束，
    //    才能安全 stop/destroy/卸库（避免写回路径上的 use-after-free）。
    host_->retireOwner(p.owner);
    host_->waitQuiescent(p.owner);
    // 2) stop（若已启动）+ destroy（在插件自身堆释放，本函数 noexcept 故须兜底异常）。
    if (p.instance.self != nullptr) {
        if (p.started) {
            guardedCall("stop", p.info.id, false,
                        [&] { return p.instance.stop(p.instance.self) != 0; });
            p.started = false;
        }
        guardedCall("destroy", p.info.id, false,
                    [&] { return p.instance.destroy(p.instance.self) != 0; });
        p.instance.self = nullptr;
    }
    // 3) 删除归属状态并卸库。
    host_->removeOwner(p.owner);
    if (p.handle != nullptr) {
        closeLibrary(p.handle);
        p.handle = nullptr;
    }
    plugins_.erase(plugins_.begin() + static_cast<std::ptrdiff_t>(index));
}

bool PluginManager::unload(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t idx = findIndexLocked(id);
    if (idx == static_cast<std::size_t>(-1)) {
        IR_LOG_WARN("卸载失败：未找到插件 id={}", id);
        return false;
    }
    IR_LOG_INFO("卸载插件 id={}", id);
    unloadLocked(idx);
    return true;
}

bool PluginManager::reload(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t idx = findIndexLocked(id);
    if (idx == static_cast<std::size_t>(-1)) {
        IR_LOG_WARN("reload 失败：未找到插件 id={}", id);
        return false;
    }
    // 先记下原 path/config 与运行状态，卸载后以同样参数重载。
    const std::string path = plugins_[idx].path;
    const std::string configPath = plugins_[idx].configPath;
    const bool wasStarted = plugins_[idx].started;
    IR_LOG_INFO("reload 插件 id={}（path={}）", id, path);
    unloadLocked(idx);
    if (!loadLocked(path, configPath)) {
        IR_LOG_ERROR("reload 失败：重新加载出错 id={}（旧实例已卸载）", id);
        return false;
    }
    if (wasStarted) {
        return startLocked(plugins_.back());
    }
    return true;
}

std::size_t PluginManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.size();
}

std::vector<PluginManager::PluginDesc> PluginManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginDesc> out;
    out.reserve(plugins_.size());
    for (const auto &p : plugins_) {
        out.push_back(PluginDesc{p.info.id ? p.info.id : "", p.info.name ? p.info.name : "",
                                 p.info.version ? p.info.version : "", p.started});
    }
    return out;
}

} // namespace core
