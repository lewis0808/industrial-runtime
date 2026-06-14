#include "plugin_manager/plugin_manager.hpp"

#include <filesystem>

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

} // namespace

PluginManager::PluginManager(const IrPluginHostApi *host) noexcept : host_(host) {}

PluginManager::~PluginManager() { unloadAll(); }

bool PluginManager::load(const std::string &path, const std::string &configPath) {
    void *handle = openLibrary(path);
    if (handle == nullptr) {
        IR_LOG_ERROR("插件加载失败，无法打开动态库: {}", path);
        return false;
    }

    auto getInfo = reinterpret_cast<irplugin::GetPluginInfoFn>(
        findSymbol(handle, IRPLUGIN_SYM_GET_PLUGIN_INFO));
    auto createPlugin =
        reinterpret_cast<irplugin::CreatePluginFn>(findSymbol(handle, IRPLUGIN_SYM_CREATE_PLUGIN));
    if (getInfo == nullptr || createPlugin == nullptr) {
        // 自动发现目录里可能混有非插件动态库（如插件的依赖库），跳过属正常情况。
        IR_LOG_INFO("跳过非插件动态库（无 {}/{}）: {}", IRPLUGIN_SYM_GET_PLUGIN_INFO,
                    IRPLUGIN_SYM_CREATE_PLUGIN, path);
        closeLibrary(handle);
        return false;
    }

    const IrPluginInfo info = getInfo();
    // 结构体仅末尾追加，宿主向后兼容：接受 <= 自身 ABI 版本的插件。
    if (info.abi_version > IRPLUGIN_ABI_VERSION) {
        IR_LOG_ERROR("插件 ABI 版本过新（插件 {} > 宿主 {}）: {}", info.abi_version,
                     IRPLUGIN_ABI_VERSION, path);
        closeLibrary(handle);
        return false;
    }

    // 把配置文件路径原样传给插件（宿主不读取内容，由插件自行解析）。
    irplugin::IPlugin *plugin = createPlugin(host_, configPath.c_str());
    if (plugin == nullptr) {
        IR_LOG_ERROR("插件 createPlugin 返回空: {}", path);
        closeLibrary(handle);
        return false;
    }

    if (!plugin->init()) {
        IR_LOG_ERROR("插件 init 失败: {}", info.id ? info.id : "?");
        plugin->destroy();
        closeLibrary(handle);
        return false;
    }

    plugins_.push_back(Loaded{handle, info, plugin, false});
    IR_LOG_INFO("插件已加载: {} ({})", info.name ? info.name : "?",
                info.version ? info.version : "?");
    return true;
}

std::size_t PluginManager::loadDirectory(const std::string &pluginDir,
                                         const std::string &configDir) {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const char *ext = ".dll";
#elif defined(__APPLE__)
    const char *ext = ".dylib";
#else
    const char *ext = ".so";
#endif
    std::error_code ec;
    if (!fs::is_directory(pluginDir, ec)) {
        IR_LOG_INFO("插件目录不存在，跳过自动发现: {}", pluginDir);
        return 0;
    }
    std::size_t loaded = 0;
    for (const auto &entry : fs::directory_iterator(pluginDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ext) {
            continue;
        }
        // 配置文件路径：configDir/<dll basename>.json（与 plugins 目录分离）。
        const auto configPath = (fs::path(configDir) / entry.path().stem()).string() + ".json";
        if (load(entry.path().string(), configPath)) {
            ++loaded;
        }
    }
    return loaded;
}

bool PluginManager::startAll() {
    bool ok = true;
    for (auto &p : plugins_) {
        if (!p.started) {
            if (p.plugin->start()) {
                p.started = true;
            } else {
                ok = false;
                IR_LOG_ERROR("插件 start 失败: {}", p.info.id ? p.info.id : "?");
            }
        }
    }
    return ok;
}

bool PluginManager::stopAll() {
    bool ok = true;
    for (auto &p : plugins_) {
        if (p.started) {
            if (!p.plugin->stop()) {
                ok = false;
                IR_LOG_ERROR("插件 stop 失败: {}", p.info.id ? p.info.id : "?");
            }
            p.started = false;
        }
    }
    return ok;
}

IrPluginInfo PluginManager::infoAt(std::size_t index) const {
    if (index >= plugins_.size()) {
        return IrPluginInfo{};
    }
    return plugins_[index].info;
}

void PluginManager::unloadAll() noexcept {
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        if (it->plugin != nullptr) {
            if (it->started) {
                it->plugin->stop();
                it->started = false;
            }
            it->plugin->destroy(); // 在插件自身堆上释放
            it->plugin = nullptr;
        }
        if (it->handle != nullptr) {
            closeLibrary(it->handle);
            it->handle = nullptr;
        }
    }
    plugins_.clear();
}

} // namespace core
