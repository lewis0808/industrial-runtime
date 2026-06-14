#include "plugin_host/plugin_host.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "common/event.hpp"
#include "common/stream.hpp"
#include "common/tag_value.hpp"
#include "logger/logger.hpp"

namespace core {

namespace {

/// 由纳秒时间戳构造 Timestamp；0 表示取当前时间。
Timestamp toTimestamp(int64_t ns) {
    if (ns == 0) {
        return now();
    }
    return Timestamp{std::chrono::duration_cast<Timestamp::duration>(std::chrono::nanoseconds{ns})};
}

std::string toString(const IrPluginString &s) {
    if (s.data == nullptr || s.len == 0) {
        return std::string{};
    }
    return std::string{s.data, s.len};
}

Variant toVariant(const IrPluginVariant &v) {
    switch (v.type) {
    case IRPLUGIN_TYPE_BOOL:
        return Variant{v.as.boolean != 0};
    case IRPLUGIN_TYPE_INT8:
        return Variant{v.as.i8};
    case IRPLUGIN_TYPE_INT16:
        return Variant{v.as.i16};
    case IRPLUGIN_TYPE_INT32:
        return Variant{v.as.i32};
    case IRPLUGIN_TYPE_INT64:
        return Variant{v.as.i64};
    case IRPLUGIN_TYPE_UINT8:
        return Variant{v.as.u8};
    case IRPLUGIN_TYPE_UINT16:
        return Variant{v.as.u16};
    case IRPLUGIN_TYPE_UINT32:
        return Variant{v.as.u32};
    case IRPLUGIN_TYPE_UINT64:
        return Variant{v.as.u64};
    case IRPLUGIN_TYPE_FLOAT:
        return Variant{v.as.f32};
    case IRPLUGIN_TYPE_DOUBLE:
        return Variant{v.as.f64};
    case IRPLUGIN_TYPE_STRING:
        return Variant{toString(v.as.str)};
    case IRPLUGIN_TYPE_NULL:
    default:
        return Variant{};
    }
}

EventSeverity toSeverity(int32_t s) {
    switch (s) {
    case IRPLUGIN_SEV_WARNING:
        return EventSeverity::Warning;
    case IRPLUGIN_SEV_ALARM:
        return EventSeverity::Alarm;
    case IRPLUGIN_SEV_CRITICAL:
        return EventSeverity::Critical;
    case IRPLUGIN_SEV_INFO:
    default:
        return EventSeverity::Info;
    }
}

int64_t fromTimestamp(Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count();
}

/// core::Variant -> IrPluginVariant。字符串值借 strHolder 维持生命周期（同步调用内有效）。
void fromVariant(IrPluginVariant &v, const Variant &var, std::string &strHolder) {
    std::visit(
        [&](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                v.type = IRPLUGIN_TYPE_NULL;
            } else if constexpr (std::is_same_v<T, bool>) {
                v.type = IRPLUGIN_TYPE_BOOL;
                v.as.boolean = x ? 1 : 0;
            } else if constexpr (std::is_same_v<T, std::int8_t>) {
                v.type = IRPLUGIN_TYPE_INT8;
                v.as.i8 = x;
            } else if constexpr (std::is_same_v<T, std::int16_t>) {
                v.type = IRPLUGIN_TYPE_INT16;
                v.as.i16 = x;
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                v.type = IRPLUGIN_TYPE_INT32;
                v.as.i32 = x;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                v.type = IRPLUGIN_TYPE_INT64;
                v.as.i64 = x;
            } else if constexpr (std::is_same_v<T, std::uint8_t>) {
                v.type = IRPLUGIN_TYPE_UINT8;
                v.as.u8 = x;
            } else if constexpr (std::is_same_v<T, std::uint16_t>) {
                v.type = IRPLUGIN_TYPE_UINT16;
                v.as.u16 = x;
            } else if constexpr (std::is_same_v<T, std::uint32_t>) {
                v.type = IRPLUGIN_TYPE_UINT32;
                v.as.u32 = x;
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                v.type = IRPLUGIN_TYPE_UINT64;
                v.as.u64 = x;
            } else if constexpr (std::is_same_v<T, float>) {
                v.type = IRPLUGIN_TYPE_FLOAT;
                v.as.f32 = x;
            } else if constexpr (std::is_same_v<T, double>) {
                v.type = IRPLUGIN_TYPE_DOUBLE;
                v.as.f64 = x;
            } else if constexpr (std::is_same_v<T, std::string>) {
                v.type = IRPLUGIN_TYPE_STRING;
                strHolder = x;
                v.as.str = {strHolder.data(), strHolder.size()};
            }
        },
        var);
}

StreamType toStreamType(int32_t t) {
    switch (t) {
    case IRPLUGIN_STREAM_FRAME:
        return StreamType::Frame;
    case IRPLUGIN_STREAM_POINTCLOUD:
        return StreamType::PointCloud;
    case IRPLUGIN_STREAM_BINARY:
    default:
        return StreamType::Binary;
    }
}

} // namespace

PluginHost::PluginHost(RuntimeApi &api) noexcept : api_(&api) {
    abi_.ctx = this;
    abi_.push_tag = &PluginHost::pushTagThunk;
    abi_.push_event = &PluginHost::pushEventThunk;
    abi_.push_stream = &PluginHost::pushStreamThunk;
    abi_.register_writer = &PluginHost::registerWriterThunk;
}

int PluginHost::pushTagThunk(void *ctx, const IrPluginTagValue *tag) noexcept {
    if (ctx == nullptr || tag == nullptr) {
        return 0;
    }
    auto *self = static_cast<PluginHost *>(ctx);
    try {
        TagValue tv;
        tv.name = toString(tag->name);
        tv.value = toVariant(tag->value);
        tv.type = dataTypeOf(tv.value);
        tv.timestamp = toTimestamp(tag->timestamp_ns);
        return self->api_->pushTag(tv) ? 1 : 0;
    } catch (...) {
        // 宿主侧异常不得逃逸回插件（跨 C-ABI 即 UB），按丢弃处理。
        return 0;
    }
}

int PluginHost::pushEventThunk(void *ctx, const IrPluginEvent *event) noexcept {
    if (ctx == nullptr || event == nullptr) {
        return 0;
    }
    auto *self = static_cast<PluginHost *>(ctx);
    try {
        Event ev{toString(event->source), toString(event->category), toString(event->message),
                 toSeverity(event->severity), toTimestamp(event->timestamp_ns)};
        return self->api_->pushEvent(ev) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int PluginHost::pushStreamThunk(void *ctx, const IrPluginStreamFrame *frame) noexcept {
    if (ctx == nullptr || frame == nullptr) {
        return 0;
    }
    auto *self = static_cast<PluginHost *>(ctx);
    try {
        std::vector<std::uint8_t> payload;
        if (frame->payload != nullptr && frame->payload_len > 0) {
            payload.assign(frame->payload, frame->payload + frame->payload_len);
        }
        StreamFrame sf{toString(frame->source), toStreamType(frame->type), std::move(payload),
                       toTimestamp(frame->timestamp_ns)};
        return self->api_->pushStream(sf) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void PluginHost::registerWriterThunk(void *ctx, const char *prefix, void *pluginCtx,
                                     IrPluginWriteFn handler) noexcept {
    if (ctx == nullptr || prefix == nullptr || handler == nullptr) {
        return;
    }
    auto *self = static_cast<PluginHost *>(ctx);
    try {
        self->writers_.push_back(WriteReg{std::string(prefix), pluginCtx, handler});
    } catch (...) {
        // 注册期内存不足等：放弃该注册，不让异常逃逸回插件。
        IR_LOG_ERROR("插件写回注册失败（前缀 {}）：宿主侧异常已拦截", prefix);
    }
}

bool PluginHost::write(const TagValue &tag) {
    for (const auto &reg : writers_) {
        // 按 topic 前缀归属：首个匹配前缀的插件负责。
        if (tag.name.size() >= reg.prefix.size() &&
            tag.name.compare(0, reg.prefix.size(), reg.prefix) == 0) {
            if (reg.handler == nullptr) {
                return false;
            }
            IrPluginTagValue c{};
            c.name = {tag.name.data(), tag.name.size()};
            c.timestamp_ns = fromTimestamp(tag.timestamp);
            std::string strHolder;
            fromVariant(c.value, tag.value, strHolder);
            // 写回处理器是插件代码，经 C-ABI 调用，异常逃逸即 UB，宿主侧拦截按未受理处理。
            try {
                return reg.handler(reg.pluginCtx, &c) > 0;
            } catch (const std::exception &e) {
                IR_LOG_ERROR("插件写回处理器抛出异常（前缀 {}）：{}", reg.prefix, e.what());
                return false;
            } catch (...) {
                IR_LOG_ERROR("插件写回处理器抛出未知异常（前缀 {}）", reg.prefix);
                return false;
            }
        }
    }
    return false;
}

} // namespace core
