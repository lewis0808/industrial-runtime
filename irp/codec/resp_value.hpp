#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace irp {

// resp1 编码的内存表示。一个 IRP 帧对应一个顶层 RespValue。
// 编解码细节见 irp/encoding/resp1.md。

struct RespNull {};

struct RespSimple {
    std::string text;
};

struct RespError {
    std::string code;     ///< 大写错误码（见 protocol/error.md）
    std::string message;  ///< 人类可读补充
};

struct RespInteger {
    std::int64_t value{0};
};

/// bulk：二进制安全（可承载数值的小端原始字节或 UTF-8 文本）。
struct RespBulk {
    std::string data;
};

struct RespArray;
struct RespMap;

using RespValue =
    std::variant<RespNull, RespSimple, RespError, RespInteger, RespBulk, RespArray, RespMap>;

struct RespArray {
    std::vector<RespValue> items;
};

/// map：键值对（key 通常为 bulk 字符串）。用于可扩展结构（TagValue/Event/HELLO）。
struct RespMap {
    std::vector<std::pair<RespValue, RespValue>> entries;
};

// ---- 便利构造 ----

[[nodiscard]] inline RespValue makeSimple(std::string s) {
    return RespSimple{std::move(s)};
}
[[nodiscard]] inline RespValue makeError(std::string code, std::string message) {
    return RespError{std::move(code), std::move(message)};
}
[[nodiscard]] inline RespValue makeInteger(std::int64_t v) {
    return RespInteger{v};
}
[[nodiscard]] inline RespValue makeBulk(std::string data) {
    return RespBulk{std::move(data)};
}
[[nodiscard]] inline RespValue makeNull() {
    return RespNull{};
}

}  // namespace irp
