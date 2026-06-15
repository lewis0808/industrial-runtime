#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "codec/irsp_value.hpp"

namespace irsp {

/// IRSP 编码层标识。语义层恒定，连接时由 HELLO 协商。详见 irsp/README.md。
enum class Encoding {
    Irsp1,   ///< V1：RESP 风格 + length-prefixed 二进制
    Msgpack, ///< V2：MessagePack
};

[[nodiscard]] inline std::string_view encodingName(Encoding e) {
    return e == Encoding::Msgpack ? "msgpack" : "irsp1";
}

[[nodiscard]] inline bool parseEncoding(std::string_view s, Encoding &out) {
    if (s == "irsp1") {
        out = Encoding::Irsp1;
        return true;
    }
    if (s == "msgpack") {
        out = Encoding::Msgpack;
        return true;
    }
    return false;
}

/// 按帧首字节判定编码（用于服务端容忍混合客户端）。
/// msgpack 顶层恒为 array/map：首字节落在 fixmap/fixarray(0x80–0x9f) 或
/// map/array16-32(0xdc–0xdf)。irsp1 首字节恒为 ASCII（`+ - : $ * %` 或 inline 文本，
/// 均 < 0x80），与上述区间不重叠，故首字节即可无歧义区分。
[[nodiscard]] inline Encoding sniffEncoding(std::string_view frame) {
    if (frame.empty()) {
        return Encoding::Irsp1;
    }
    const auto b = static_cast<unsigned char>(frame[0]);
    const bool msgpackContainer = (b >= 0x80 && b <= 0x9f) || (b >= 0xdc && b <= 0xdf);
    return msgpackContainer ? Encoding::Msgpack : Encoding::Irsp1;
}

/// 编解码统一结果（跨编码门面）。
enum class CodecStatus {
    Ok,         ///< 成功解析出一个完整顶层值
    Incomplete, ///< 数据不完整，需更多字节
    Error,      ///< 协议违规（坏帧）
};

struct CodecDecode {
    CodecStatus status{CodecStatus::Error};
    IrspValue value{IrspNull{}};
    std::size_t consumed{0}; ///< 已消费字节数（Ok 时有效）
};

/// 按指定编码序列化一个顶层值。
[[nodiscard]] std::string encodeValue(Encoding enc, const IrspValue &value);

/// 按指定编码从 buffer 起始解析一个顶层值。
[[nodiscard]] CodecDecode decodeValue(Encoding enc, std::string_view buffer);

} // namespace irsp
