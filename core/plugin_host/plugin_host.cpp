#include "plugin_host/plugin_host.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
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

PluginHost::PluginHost(RuntimeApi &api) : api_(&api) {
    abi_.ctx = this;
    abi_.push_tag = &PluginHost::pushTagThunk;
    abi_.push_event = &PluginHost::pushEventThunk;
    abi_.push_stream = &PluginHost::pushStreamThunk;
    abi_.register_writer = &PluginHost::registerWriterThunk;
    // owner 0 = 无归属：经 abi() 直接注册（运行时自身/测试）的写回挂于此，永不撤销。
    owners_.emplace(0, std::make_shared<OwnerState>());
}

std::shared_ptr<PluginHost::OwnerState> PluginHost::ownerStateLocked(OwnerId owner) const {
    auto it = owners_.find(owner);
    return it == owners_.end() ? nullptr : it->second;
}

PluginHost::OwnerId PluginHost::createOwner() {
    std::unique_lock lock(writersMutex_);
    const OwnerId id = nextOwner_++;
    owners_.emplace(id, std::make_shared<OwnerState>());
    return id;
}

void PluginHost::setActiveOwner(OwnerId owner) noexcept {
    std::unique_lock lock(writersMutex_);
    activeOwner_ = owner;
}

void PluginHost::retireOwner(OwnerId owner) {
    if (owner == 0) {
        return; // 无归属项不可撤销
    }
    std::unique_lock lock(writersMutex_);
    auto state = ownerStateLocked(owner);
    if (state == nullptr) {
        return;
    }
    // 移除该 owner 的全部写回：此后 write() 在共享锁下选不到它，不会再自增其 inflight。
    std::erase_if(writers_, [&](const WriteReg &reg) { return reg.owner == state; });
}

void PluginHost::waitQuiescent(OwnerId owner) const {
    std::shared_ptr<OwnerState> state;
    {
        std::shared_lock lock(writersMutex_);
        state = ownerStateLocked(owner);
    }
    if (state == nullptr) {
        return;
    }
    // retireOwner 已在独占锁下摘除其写回，故不会有新的自增；等待已在途的调用自减归零。
    // 卸载是低频管理操作，让出 CPU 轮询即可。
    while (state->inflight.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
}

void PluginHost::removeOwner(OwnerId owner) noexcept {
    if (owner == 0) {
        return;
    }
    std::unique_lock lock(writersMutex_);
    owners_.erase(owner);
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
        std::unique_lock lock(self->writersMutex_);
        // 同前缀冲突告警：完全相同的 prefix 无法区分归属，最长前缀相同时以先注册者为准。
        // （`a/` 与 `a/b/` 属正常分层，不在此列。）
        for (const auto &reg : self->writers_) {
            if (reg.prefix == prefix) {
                IR_LOG_WARN("写回前缀冲突：'{}' 已被注册，新处理器将被忽略（以先注册者为准）",
                            prefix);
                return;
            }
        }
        // 归于当前 active owner（PluginManager 在调用插件 init/start 期间设定）；其外注册
        // 归于 owner 0（无归属，永不撤销）。owner 状态在加载时已建，正常不会缺失。
        auto state = self->ownerStateLocked(self->activeOwner_);
        if (state == nullptr) {
            state = self->owners_[0];
        }
        self->writers_.push_back(WriteReg{std::string(prefix), pluginCtx, handler, std::move(state)});
    } catch (...) {
        // 注册期内存不足等：放弃该注册，不让异常逃逸回插件。
        IR_LOG_ERROR("插件写回注册失败（前缀 {}）：宿主侧异常已拦截", prefix);
    }
}

bool PluginHost::write(const TagValue &tag) {
    // 锁内只选出最长前缀匹配的处理器、拷出并**自增其归属在途计数**，出锁后再调用插件代码——
    // 不持锁执行外部代码（避免长持锁 / 潜在死锁）。inflight 在锁内自增是热卸载安全的关键：
    // retireOwner 在独占锁下摘除写回，与本处选取互斥，故选中即说明 owner 未被卸载，自增的
    // 计数令 waitQuiescent 等到本次调用结束才放行 destroy/卸库。
    void *pluginCtx = nullptr;
    IrPluginWriteFn handler = nullptr;
    std::shared_ptr<OwnerState> owner;
    std::size_t bestLen = 0;
    bool matched = false;
    {
        std::shared_lock lock(writersMutex_);
        for (const auto &reg : writers_) {
            if (reg.handler == nullptr || tag.name.size() < reg.prefix.size() ||
                tag.name.compare(0, reg.prefix.size(), reg.prefix) != 0) {
                continue;
            }
            // 最长前缀胜出；长度相同（同前缀，注册时已去重）保留先注册者。
            if (!matched || reg.prefix.size() > bestLen) {
                bestLen = reg.prefix.size();
                pluginCtx = reg.pluginCtx;
                handler = reg.handler;
                owner = reg.owner;
                matched = true;
            }
        }
        if (matched) {
            owner->inflight.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    if (!matched) {
        return false;
    }

    IrPluginTagValue c{};
    c.name = {tag.name.data(), tag.name.size()};
    c.timestamp_ns = fromTimestamp(tag.timestamp);
    std::string strHolder;
    fromVariant(c.value, tag.value, strHolder);
    // 写回处理器是插件代码，经 C-ABI 调用，异常逃逸即 UB，宿主侧拦截按未受理处理。
    bool accepted = false;
    try {
        accepted = handler(pluginCtx, &c) > 0;
    } catch (const std::exception &e) {
        IR_LOG_ERROR("插件写回处理器抛出异常：{}", e.what());
    } catch (...) {
        IR_LOG_ERROR("插件写回处理器抛出未知异常");
    }
    owner->inflight.fetch_sub(1, std::memory_order_acq_rel);
    return accepted;
}

} // namespace core
