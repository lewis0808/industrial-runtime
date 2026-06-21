#pragma once

// 插件作者面向的 C++ 头：实现 IPlugin，用 Host 推送数据。
// 仅依赖 plugin_abi.h（纯 C ABI），不与 core 共享任何 C++ 类型。

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include "irplugin/plugin_abi.h"

// 插件导出宏。导出 extern "C" 符号，避免名字修饰。
#if defined(_WIN32)
#define IRPLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define IRPLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace irplugin {

/// 插件接口。纯抽象、无数据成员、方法不收发 STL、不抛异常跨边界。
///
/// 生命周期：createPlugin -> init -> start -> ... -> stop -> destroy。
/// destroy() 通常实现为 `delete this`，以确保在插件自己的堆上释放。
class IPlugin {
  public:
    virtual ~IPlugin() = default;

    virtual bool init() = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool destroy() = 0;
};

/// 宿主句柄的轻量 C++ 封装：把 C++ 值封送到 C 结构并调用 IrPluginHostApi。
///
/// 所有 push* 为同步调用，宿主在返回前完成拷贝，故栈上临时结构与字符串视图安全。
class Host {
  public:
    Host() = default;
    explicit Host(const IrPluginHostApi *api) noexcept : api_(api) {}

    [[nodiscard]] bool valid() const noexcept {
        return api_ != nullptr && api_->push_tag != nullptr;
    }

    /// 推送算术类型 Tag（bool/整型/浮点）。
    template <typename T>
        requires std::is_arithmetic_v<T>
    bool pushTag(std::string_view name, T value, int64_t timestampNs = 0) const {
        IrPluginTagValue tag{};
        tag.name = {name.data(), name.size()};
        tag.timestamp_ns = timestampNs;
        fillVariant(tag.value, value);
        return api_->push_tag(api_->ctx, &tag) != 0;
    }

    /// 推送字符串 Tag。
    bool pushTag(std::string_view name, std::string_view value, int64_t timestampNs = 0) const {
        IrPluginTagValue tag{};
        tag.name = {name.data(), name.size()};
        tag.timestamp_ns = timestampNs;
        tag.value.type = IRPLUGIN_TYPE_STRING;
        tag.value.as.str = {value.data(), value.size()};
        return api_->push_tag(api_->ctx, &tag) != 0;
    }

    /// 推送事件。
    bool pushEvent(std::string_view source, std::string_view category, std::string_view message,
                   IrPluginSeverity severity = IRPLUGIN_SEV_INFO, int64_t timestampNs = 0) const {
        IrPluginEvent ev{};
        ev.source = {source.data(), source.size()};
        ev.category = {category.data(), category.size()};
        ev.message = {message.data(), message.size()};
        ev.severity = static_cast<int32_t>(severity);
        ev.timestamp_ns = timestampNs;
        return api_->push_event(api_->ctx, &ev) != 0;
    }

    /// 推送一帧流数据。
    bool pushStream(std::string_view source, IrPluginStreamType type, const uint8_t *payload,
                    size_t payloadLen, int64_t timestampNs = 0) const {
        IrPluginStreamFrame frame{};
        frame.source = {source.data(), source.size()};
        frame.type = static_cast<int32_t>(type);
        frame.timestamp_ns = timestampNs;
        frame.payload = payload;
        frame.payload_len = payloadLen;
        return api_->push_stream(api_->ctx, &frame) != 0;
    }

    /// 注册写回处理器：声明本插件负责 prefix 前缀下 Tag 的写。
    /// 应用 SET 命中该前缀时，宿主回调 cb(写入的 TagValue)，返回 true 表示已受理。
    /// 注意：注册后请勿拷贝/移动本 Host 对象（蹦床以 this 为上下文）。
    void onWrite(std::string_view prefix, std::function<bool(const IrPluginTagValue &)> cb) {
        writeCb_ = std::move(cb);
        writePrefix_ = std::string(prefix);
        if (api_ != nullptr && api_->register_writer != nullptr) {
            api_->register_writer(api_->ctx, writePrefix_.c_str(), this, &Host::writeTrampoline);
        }
    }

  private:
    static int writeTrampoline(void *plugin_ctx, const IrPluginTagValue *tag) {
        auto *self = static_cast<Host *>(plugin_ctx);
        if (self == nullptr || !self->writeCb_ || tag == nullptr) {
            return 0;
        }
        return self->writeCb_(*tag) ? 1 : 0;
    }

    template <typename T> static void fillVariant(IrPluginVariant &v, T value) noexcept {
        if constexpr (std::is_same_v<T, bool>) {
            v.type = IRPLUGIN_TYPE_BOOL;
            v.as.boolean = value ? 1 : 0;
        } else if constexpr (std::is_same_v<T, float>) {
            v.type = IRPLUGIN_TYPE_FLOAT;
            v.as.f32 = value;
        } else if constexpr (std::is_same_v<T, double>) {
            v.type = IRPLUGIN_TYPE_DOUBLE;
            v.as.f64 = value;
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            if constexpr (sizeof(T) == 1) {
                v.type = IRPLUGIN_TYPE_INT8;
                v.as.i8 = value;
            } else if constexpr (sizeof(T) == 2) {
                v.type = IRPLUGIN_TYPE_INT16;
                v.as.i16 = value;
            } else if constexpr (sizeof(T) == 4) {
                v.type = IRPLUGIN_TYPE_INT32;
                v.as.i32 = value;
            } else {
                v.type = IRPLUGIN_TYPE_INT64;
                v.as.i64 = static_cast<int64_t>(value);
            }
        } else { // 无符号整型
            if constexpr (sizeof(T) == 1) {
                v.type = IRPLUGIN_TYPE_UINT8;
                v.as.u8 = value;
            } else if constexpr (sizeof(T) == 2) {
                v.type = IRPLUGIN_TYPE_UINT16;
                v.as.u16 = value;
            } else if constexpr (sizeof(T) == 4) {
                v.type = IRPLUGIN_TYPE_UINT32;
                v.as.u32 = value;
            } else {
                v.type = IRPLUGIN_TYPE_UINT64;
                v.as.u64 = static_cast<uint64_t>(value);
            }
        }
    }

    const IrPluginHostApi *api_ = nullptr;
    std::function<bool(const IrPluginTagValue &)> writeCb_;
    std::string writePrefix_;
};

namespace detail {

/// 生命周期蹦床：把 C vtable 调用转回 IPlugin 的虚函数。
/// 关键：这些调用**全部停留在插件 DLL 内部**，故 IPlugin 的 C++ vtable 布局不再跨越 DLL 边界
/// ——宿主只调 C 函数指针。这正是「任意语言/编译器写插件」得以成立的前提。
inline int abiInit(void *self) { return static_cast<IPlugin *>(self)->init() ? 1 : 0; }
inline int abiStart(void *self) { return static_cast<IPlugin *>(self)->start() ? 1 : 0; }
inline int abiStop(void *self) { return static_cast<IPlugin *>(self)->stop() ? 1 : 0; }
inline int abiDestroy(void *self) { return static_cast<IPlugin *>(self)->destroy() ? 1 : 0; }

} // namespace detail

/// 把已构造的 IPlugin 实例封装进 C vtable，供 createPlugin 填充 out 使用。
/// 用法：`return irplugin::makeInstance(new (std::nothrow) MyPlugin(host), out);`
/// plugin 或 out 为空均返回 0（失败）；plugin 非空而 out 为空时顺手 delete 以免泄漏。
inline int makeInstance(IPlugin *plugin, IrPluginInstance *out) noexcept {
    if (out == nullptr) {
        delete plugin;
        return 0;
    }
    if (plugin == nullptr) {
        return 0;
    }
    out->self = plugin;
    out->init = &detail::abiInit;
    out->start = &detail::abiStart;
    out->stop = &detail::abiStop;
    out->destroy = &detail::abiDestroy;
    return 1;
}

/// 宿主侧用于解析导出函数的指针类型（与 plugin_abi.h 的 C 类型同一）。
/// createPlugin(host, config_path, out)：成功返回 1 并填充 *out（见 makeInstance），失败返回 0。
/// config_path 为该插件配置文件的完整路径（UTF-8）。宿主按约定算出
/// `<exe>/config/<dll basename>.json` 传入（仅传路径，宿主不读取/不解析内容）；
/// 插件自行读取并按需热扫描该文件，文件不存在时应回退内置默认值。无需配置的插件忽略它。
using GetPluginInfoFn = IrPluginGetInfoFn;
using CreatePluginFn = IrPluginCreateFn;

} // namespace irplugin
