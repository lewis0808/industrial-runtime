#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
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

// ---- DataType ↔ Variant 契约锁（编译期）----
// dataTypeOf() 依赖「DataType 取值 == 对应类型在 Variant 中的 index」这一隐式契约。
// 下面把它显式锁死：枚举值或 Variant 备选顺序任一处重排，都会在此静态失败，
// 而非运行期静默错位。每条断言同时校验 ① 枚举值即该备选的 index、② 该 index 处确为预期类型。
namespace detail {
template <DataType D, typename T>
inline constexpr bool variantAltIs =
    std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(D), Variant>, T>;
} // namespace detail

static_assert(std::variant_size_v<Variant> == static_cast<std::size_t>(DataType::String) + 1,
              "DataType 枚举数量与 Variant 备选数量不一致");
static_assert(detail::variantAltIs<DataType::Null, std::monostate>);
static_assert(detail::variantAltIs<DataType::Bool, bool>);
static_assert(detail::variantAltIs<DataType::Int8, std::int8_t>);
static_assert(detail::variantAltIs<DataType::Int16, std::int16_t>);
static_assert(detail::variantAltIs<DataType::Int32, std::int32_t>);
static_assert(detail::variantAltIs<DataType::Int64, std::int64_t>);
static_assert(detail::variantAltIs<DataType::UInt8, std::uint8_t>);
static_assert(detail::variantAltIs<DataType::UInt16, std::uint16_t>);
static_assert(detail::variantAltIs<DataType::UInt32, std::uint32_t>);
static_assert(detail::variantAltIs<DataType::UInt64, std::uint64_t>);
static_assert(detail::variantAltIs<DataType::Float, float>);
static_assert(detail::variantAltIs<DataType::Double, double>);
static_assert(detail::variantAltIs<DataType::String, std::string>);

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