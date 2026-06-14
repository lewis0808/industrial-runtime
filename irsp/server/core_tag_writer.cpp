#include "server/core_tag_writer.hpp"

#include <cstdint>
#include <cstring>
#include <optional>

#include "common/tag_value.hpp"
#include "runtime_engine/runtime_engine.hpp"

namespace irsp {

namespace {

template <typename T> T readLE(const std::string &b) {
    T v{};
    std::memcpy(&v, b.data(), sizeof(T));
    return v;
}

/// (类型标签, 字节) -> core::Variant；类型未知或长度不足返回 nullopt。
std::optional<core::Variant> toVariant(const std::string &type, const std::string &b) {
    const auto has = [&](std::size_t n) { return b.size() >= n; };
    if (type == "null")
        return core::Variant{};
    if (type == "bool")
        return core::Variant{!b.empty() && b[0] != 0};
    if (type == "i8" && has(1))
        return core::Variant{static_cast<std::int8_t>(b[0])};
    if (type == "i16" && has(2))
        return core::Variant{readLE<std::int16_t>(b)};
    if (type == "i32" && has(4))
        return core::Variant{readLE<std::int32_t>(b)};
    if (type == "i64" && has(8))
        return core::Variant{readLE<std::int64_t>(b)};
    if (type == "u8" && has(1))
        return core::Variant{static_cast<std::uint8_t>(b[0])};
    if (type == "u16" && has(2))
        return core::Variant{readLE<std::uint16_t>(b)};
    if (type == "u32" && has(4))
        return core::Variant{readLE<std::uint32_t>(b)};
    if (type == "u64" && has(8))
        return core::Variant{readLE<std::uint64_t>(b)};
    if (type == "f32" && has(4))
        return core::Variant{readLE<float>(b)};
    if (type == "f64" && has(8))
        return core::Variant{readLE<double>(b)};
    if (type == "str")
        return core::Variant{b};
    return std::nullopt;
}

} // namespace

WriteResult CoreTagWriter::write(const std::string &name, const std::string &type,
                                 const std::string &value) {
    auto var = toVariant(type, value);
    if (!var) {
        return WriteResult::Error;
    }
    core::TagValue tag{name, std::move(*var)};
    return runtime_->writeTag(tag) ? WriteResult::Accepted : WriteResult::NotHandled;
}

} // namespace irsp
