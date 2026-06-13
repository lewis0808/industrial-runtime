#include "plugin_manager/plugin_manager.hpp"

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
    return ::LoadLibraryA(path.c_str());
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

bool PluginManager::load(const std::string &path) {
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
        IR_LOG_ERROR("插件缺少导出符号 {}/{}: {}", IRPLUGIN_SYM_GET_PLUGIN_INFO,
                     IRPLUGIN_SYM_CREATE_PLUGIN, path);
        closeLibrary(handle);
        return false;
    }

    const IrPluginInfo info = getInfo();
    if (info.abi_version != IRPLUGIN_ABI_VERSION) {
        IR_LOG_ERROR("插件 ABI 版本不匹配（插件 {} != 宿主 {}）: {}", info.abi_version,
                     IRPLUGIN_ABI_VERSION, path);
        closeLibrary(handle);
        return false;
    }

    irplugin::IPlugin *plugin = createPlugin(host_);
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
