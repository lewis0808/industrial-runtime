#include "server/core_tag_source.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <variant>

#include "semantic/topic.hpp"

namespace irsp {

namespace {

struct Encoded {
    std::string type;
    std::string bytes;
};

/// 把定长数值原样拷为字节（主机字节序，x64 为小端）。
template <typename T> std::string rawBytes(T value) {
    std::string s(sizeof(T), '\0');
    std::memcpy(s.data(), &value, sizeof(T));
    return s;
}

Encoded encodeVariant(const core::Variant &v) {
    return std::visit(
        [](const auto &x) -> Encoded {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {"null", std::string{}};
            } else if constexpr (std::is_same_v<T, bool>) {
                return {"bool", std::string(1, x ? '\x01' : '\x00')};
            } else if constexpr (std::is_same_v<T, std::int8_t>) {
                return {"i8", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::int16_t>) {
                return {"i16", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::int32_t>) {
                return {"i32", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return {"i64", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::uint8_t>) {
                return {"u8", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::uint16_t>) {
                return {"u16", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::uint32_t>) {
                return {"u32", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                return {"u64", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, float>) {
                return {"f32", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, double>) {
                return {"f64", rawBytes(x)};
            } else if constexpr (std::is_same_v<T, std::string>) {
                return {"str", x};
            }
        },
        v);
}

std::int64_t toNs(core::Timestamp ts) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(ts.time_since_epoch()).count();
}

} // namespace

std::optional<TagRecord> CoreTagSource::read(const std::string &name) const {
    auto tag = engine_->read(name);
    if (!tag) {
        return std::nullopt;
    }
    Encoded enc = encodeVariant(tag->value);
    return TagRecord{tag->name, std::move(enc.type), toNs(tag->timestamp), std::move(enc.bytes)};
}

bool CoreTagSource::exists(const std::string &name) const { return engine_->exists(name); }

ScanResult CoreTagSource::scan(const std::string &cursor, const std::string &pattern,
                               std::size_t count) const {
    const std::string start = (cursor == "0") ? std::string{} : cursor;
    const std::size_t limit = count == 0 ? 1000 : count;

    std::vector<std::string> names;
    for (const auto &tag : engine_->snapshot()) {
        if (tag.name > start && TopicMatcher::matches(pattern, tag.name)) {
            names.push_back(tag.name);
        }
    }
    std::sort(names.begin(), names.end());

    ScanResult result;
    if (names.size() <= limit) {
        result.names = std::move(names);
        result.nextCursor = "0"; // 遍历结束
    } else {
        result.names.assign(names.begin(), names.begin() + static_cast<std::ptrdiff_t>(limit));
        result.nextCursor = result.names.back();
    }
    return result;
}

} // namespace irsp
