#include "plugin_host/plugin_host.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "common/event.hpp"
#include "common/stream.hpp"
#include "common/tag_value.hpp"

namespace core {

namespace {

/// 由纳秒时间戳构造 Timestamp；0 表示取当前时间。
Timestamp toTimestamp(int64_t ns) {
    if (ns == 0) {
        return now();
    }
    return Timestamp{std::chrono::duration_cast<Timestamp::duration>(
        std::chrono::nanoseconds{ns})};
}

std::string toString(const IrPluginString& s) {
    if (s.data == nullptr || s.len == 0) {
        return std::string{};
    }
    return std::string{s.data, s.len};
}

Variant toVariant(const IrPluginVariant& v) {
    switch (v.type) {
        case IRPLUGIN_TYPE_BOOL:   return Variant{v.as.boolean != 0};
        case IRPLUGIN_TYPE_INT8:   return Variant{v.as.i8};
        case IRPLUGIN_TYPE_INT16:  return Variant{v.as.i16};
        case IRPLUGIN_TYPE_INT32:  return Variant{v.as.i32};
        case IRPLUGIN_TYPE_INT64:  return Variant{v.as.i64};
        case IRPLUGIN_TYPE_UINT8:  return Variant{v.as.u8};
        case IRPLUGIN_TYPE_UINT16: return Variant{v.as.u16};
        case IRPLUGIN_TYPE_UINT32: return Variant{v.as.u32};
        case IRPLUGIN_TYPE_UINT64: return Variant{v.as.u64};
        case IRPLUGIN_TYPE_FLOAT:  return Variant{v.as.f32};
        case IRPLUGIN_TYPE_DOUBLE: return Variant{v.as.f64};
        case IRPLUGIN_TYPE_STRING: return Variant{toString(v.as.str)};
        case IRPLUGIN_TYPE_NULL:
        default:                   return Variant{};
    }
}

EventSeverity toSeverity(int32_t s) {
    switch (s) {
        case IRPLUGIN_SEV_WARNING:  return EventSeverity::Warning;
        case IRPLUGIN_SEV_ALARM:    return EventSeverity::Alarm;
        case IRPLUGIN_SEV_CRITICAL: return EventSeverity::Critical;
        case IRPLUGIN_SEV_INFO:
        default:                    return EventSeverity::Info;
    }
}

StreamType toStreamType(int32_t t) {
    switch (t) {
        case IRPLUGIN_STREAM_FRAME:      return StreamType::Frame;
        case IRPLUGIN_STREAM_POINTCLOUD: return StreamType::PointCloud;
        case IRPLUGIN_STREAM_BINARY:
        default:                         return StreamType::Binary;
    }
}

}  // namespace

PluginHost::PluginHost(RuntimeApi& api) noexcept : api_(&api) {
    abi_.ctx = this;
    abi_.push_tag = &PluginHost::pushTagThunk;
    abi_.push_event = &PluginHost::pushEventThunk;
    abi_.push_stream = &PluginHost::pushStreamThunk;
}

int PluginHost::pushTagThunk(void* ctx, const IrPluginTagValue* tag) {
    if (ctx == nullptr || tag == nullptr) {
        return 0;
    }
    auto* self = static_cast<PluginHost*>(ctx);
    TagValue tv;
    tv.name = toString(tag->name);
    tv.value = toVariant(tag->value);
    tv.type = dataTypeOf(tv.value);
    tv.timestamp = toTimestamp(tag->timestamp_ns);
    return self->api_->pushTag(tv) ? 1 : 0;
}

int PluginHost::pushEventThunk(void* ctx, const IrPluginEvent* event) {
    if (ctx == nullptr || event == nullptr) {
        return 0;
    }
    auto* self = static_cast<PluginHost*>(ctx);
    Event ev{toString(event->source), toString(event->category),
             toString(event->message), toSeverity(event->severity),
             toTimestamp(event->timestamp_ns)};
    return self->api_->pushEvent(ev) ? 1 : 0;
}

int PluginHost::pushStreamThunk(void* ctx, const IrPluginStreamFrame* frame) {
    if (ctx == nullptr || frame == nullptr) {
        return 0;
    }
    auto* self = static_cast<PluginHost*>(ctx);
    std::vector<std::uint8_t> payload;
    if (frame->payload != nullptr && frame->payload_len > 0) {
        payload.assign(frame->payload, frame->payload + frame->payload_len);
    }
    StreamFrame sf{toString(frame->source), toStreamType(frame->type),
                   std::move(payload), toTimestamp(frame->timestamp_ns)};
    return self->api_->pushStream(sf) ? 1 : 0;
}

}  // namespace core
