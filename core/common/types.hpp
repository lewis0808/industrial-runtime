#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace core {

/// 统一时间戳：系统时钟时间点。
using Timestamp = std::chrono::system_clock::time_point;

/// 获取当前时间戳。
[[nodiscard]] inline Timestamp now() noexcept { return std::chrono::system_clock::now(); }

/// 数据类型枚举。
///
/// 取值顺序必须与 Variant 的备选类型顺序严格一致，
/// 以便通过 Variant::index() 直接映射到 DataType。
enum class DataType : std::uint8_t {
    Null = 0,
    Bool,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float,
    Double,
    String,
};

/// Tag 值变体。覆盖工业场景常用的全部标量与字符串类型。
///
/// 禁止使用 void*；二进制/图像/点云属于 Stream 体系，不得放入 Variant。
using Variant =
    std::variant<std::monostate, // Null
                 bool, std::int8_t, std::int16_t, std::int32_t, std::int64_t, std::uint8_t,
                 std::uint16_t, std::uint32_t, std::uint64_t, float, double, std::string>;

/// 由 Variant 当前持有的备选类型推导其 DataType。
[[nodiscard]] inline DataType dataTypeOf(const Variant &v) noexcept {
    return static_cast<DataType>(v.index());
}

/// 返回 DataType 的可读名称（用于日志/序列化）。
[[nodiscard]] constexpr std::string_view dataTypeName(DataType type) noexcept {
    switch (type) {
    case DataType::Null:
        return "Null";
    case DataType::Bool:
        return "Bool";
    case DataType::Int8:
        return "Int8";
    case DataType::Int16:
        return "Int16";
    case DataType::Int32:
        return "Int32";
    case DataType::Int64:
        return "Int64";
    case DataType::UInt8:
        return "UInt8";
    case DataType::UInt16:
        return "UInt16";
    case DataType::UInt32:
        return "UInt32";
    case DataType::UInt64:
        return "UInt64";
    case DataType::Float:
        return "Float";
    case DataType::Double:
        return "Double";
    case DataType::String:
        return "String";
    }
    return "Unknown";
}

} // namespace core