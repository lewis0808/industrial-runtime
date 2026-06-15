#pragma once

#include <string>
#include <string_view>

#include "codec/encoding.hpp"
#include "codec/irsp_value.hpp"

namespace irsp {

/// msgpack 编解码（IRSP V2 编码）：IrspValue <-> MessagePack 字节。
///
/// 语义层与帧结构语义不变，仅替换字节编码。映射详见 irsp/doc/encoding/msgpack.md：
/// - Null→nil，Integer→int，Array→array，Map→map；
/// - Bulk/Simple→str（编码方向 bulk 仅承载文本：名/游标/PING）；
/// - Error→`{err,msg}` map（doc 约定）；
/// - TypedValue→依 type 编码为原生 int/float/bool/str（这是 V2 相对 V1 的核心收益）。
class MsgpackCodec {
  public:
    /// 编码一个值，追加到 out。
    static void encode(const IrspValue &value, std::string &out);

    /// 编码一个值并返回字节串。
    [[nodiscard]] static std::string encode(const IrspValue &value);

    /// 从 buffer 起始解析一个顶层值。
    [[nodiscard]] static CodecDecode decode(std::string_view buffer);
};

} // namespace irsp