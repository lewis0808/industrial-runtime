#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "codec/irsp_value.hpp"

namespace irsp {

/// irsp1 编解码：IrspValue <-> 字节。详见 irsp/doc/encoding/irsp1.md。
class Irsp1Codec {
  public:
    enum class Status {
        Ok,         ///< 成功解析出一个完整顶层值
        Incomplete, ///< 数据不完整，需更多字节（流式传输时可继续等待）
        Error,      ///< 协议违规（坏帧）
    };

    struct DecodeResult {
        Status status{Status::Error};
        IrspValue value{IrspNull{}};
        std::size_t consumed{0}; ///< 已消费字节数（Ok 时有效）
    };

    /// 编码一个值，追加到 out。
    static void encode(const IrspValue &value, std::string &out);

    /// 编码一个值并返回字节串。
    [[nodiscard]] static std::string encode(const IrspValue &value);

    /// 从 buffer 起始解析一个顶层值。
    [[nodiscard]] static DecodeResult decode(std::string_view buffer);

    /// 解析 inline 命令（Redis 风格，调试用）：按空白把一行拆成 bulk 数组。
    /// 用于客户端直接发文本命令（如 wscat 敲 "HELLO 1"），不要求 irsp1 编码。空行返回空数组。
    [[nodiscard]] static IrspValue decodeInline(std::string_view line);
};

} // namespace irsp
