#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace irsp {

// irsp1 编码的内存表示。一个 IRSP 帧对应一个顶层 IrspValue。
// 编解码细节见 irsp/encoding/irsp1.md。

struct IrspNull {};

struct IrspSimple {
    std::string text;
};

struct IrspError {
    std::string code;    ///< 大写错误码（见 protocol/error.md）
    std::string message; ///< 人类可读补充
};

struct IrspInteger {
    std::int64_t value{0};
};

/// bulk：二进制安全（可承载数值的小端原始字节或 UTF-8 文本）。
struct IrspBulk {
    std::string data;
};

struct IrspArray;
struct IrspMap;

using IrspValue =
    std::variant<IrspNull, IrspSimple, IrspError, IrspInteger, IrspBulk, IrspArray, IrspMap>;

struct IrspArray {
    std::vector<IrspValue> items;
};

/// map：键值对（key 通常为 bulk 字符串）。用于可扩展结构（TagValue/Event/HELLO）。
struct IrspMap {
    std::vector<std::pair<IrspValue, IrspValue>> entries;
};

// ---- 便利构造 ----

[[nodiscard]] inline IrspValue makeSimple(std::string s) { return IrspSimple{std::move(s)}; }
[[nodiscard]] inline IrspValue makeError(std::string code, std::string message) {
    return IrspError{std::move(code), std::move(message)};
}
[[nodiscard]] inline IrspValue makeInteger(std::int64_t v) { return IrspInteger{v}; }
[[nodiscard]] inline IrspValue makeBulk(std::string data) { return IrspBulk{std::move(data)}; }
[[nodiscard]] inline IrspValue makeNull() { return IrspNull{}; }

} // namespace irsp
