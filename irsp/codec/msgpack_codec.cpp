#include "codec/msgpack_codec.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <variant>

#include "codec/irsp1_codec.hpp"

namespace irsp {

namespace {

// ============================ 编码 ============================

void putU8(std::string &o, std::uint8_t b) { o.push_back(static_cast<char>(b)); }

/// 追加 v 的低 n 字节，大端序（msgpack 多字节整数恒为大端）。
void putBE(std::string &o, std::uint64_t v, int n) {
    for (int i = n - 1; i >= 0; --i) {
        o.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

void encUint(std::string &o, std::uint64_t u) {
    if (u <= 0x7f) {
        putU8(o, static_cast<std::uint8_t>(u)); // positive fixint
    } else if (u <= 0xff) {
        putU8(o, 0xcc);
        putBE(o, u, 1);
    } else if (u <= 0xffff) {
        putU8(o, 0xcd);
        putBE(o, u, 2);
    } else if (u <= 0xffffffffULL) {
        putU8(o, 0xce);
        putBE(o, u, 4);
    } else {
        putU8(o, 0xcf);
        putBE(o, u, 8);
    }
}

void encSint(std::string &o, std::int64_t v) {
    if (v >= 0) {
        encUint(o, static_cast<std::uint64_t>(v));
    } else if (v >= -32) {
        putU8(o, static_cast<std::uint8_t>(v)); // negative fixint（两补码低 8 位即 0xe0..0xff）
    } else if (v >= -128) {
        putU8(o, 0xd0);
        putBE(o, static_cast<std::uint64_t>(v), 1);
    } else if (v >= -32768) {
        putU8(o, 0xd1);
        putBE(o, static_cast<std::uint64_t>(v), 2);
    } else if (v >= -2147483648LL) {
        putU8(o, 0xd2);
        putBE(o, static_cast<std::uint64_t>(v), 4);
    } else {
        putU8(o, 0xd3);
        putBE(o, static_cast<std::uint64_t>(v), 8);
    }
}

void encStr(std::string &o, const std::string &s) {
    const std::size_t n = s.size();
    if (n <= 31) {
        putU8(o, static_cast<std::uint8_t>(0xa0 | n)); // fixstr
    } else if (n <= 0xff) {
        putU8(o, 0xd9);
        putBE(o, n, 1);
    } else if (n <= 0xffff) {
        putU8(o, 0xda);
        putBE(o, n, 2);
    } else {
        putU8(o, 0xdb);
        putBE(o, n, 4);
    }
    o.append(s);
}

void encBin(std::string &o, const std::string &s) {
    const std::size_t n = s.size();
    if (n <= 0xff) {
        putU8(o, 0xc4);
        putBE(o, n, 1);
    } else if (n <= 0xffff) {
        putU8(o, 0xc5);
        putBE(o, n, 2);
    } else {
        putU8(o, 0xc6);
        putBE(o, n, 4);
    }
    o.append(s);
}

void encArrHdr(std::string &o, std::size_t n) {
    if (n <= 15) {
        putU8(o, static_cast<std::uint8_t>(0x90 | n)); // fixarray
    } else if (n <= 0xffff) {
        putU8(o, 0xdc);
        putBE(o, n, 2);
    } else {
        putU8(o, 0xdd);
        putBE(o, n, 4);
    }
}

void encMapHdr(std::string &o, std::size_t n) {
    if (n <= 15) {
        putU8(o, static_cast<std::uint8_t>(0x80 | n)); // fixmap
    } else if (n <= 0xffff) {
        putU8(o, 0xde);
        putBE(o, n, 2);
    } else {
        putU8(o, 0xdf);
        putBE(o, n, 4);
    }
}

/// 从小端原始字节读 n 字节为无符号整数。
std::uint64_t readLE(const std::string &r, std::size_t n) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(r[i])) << (8 * i);
    }
    return v;
}

/// 依类型标签把 TagValue.value（小端原始字节）编码为 msgpack 原生类型。
/// 类型未知或字节长度不符 -> 原样 bin 兜底（无损）。
void encTyped(std::string &o, const IrspTypedValue &tv) {
    const std::string &t = tv.type;
    const std::string &r = tv.raw;
    const std::size_t sz = r.size();

    if (t == "null") {
        putU8(o, 0xc0);
    } else if (t == "bool") {
        putU8(o, (sz >= 1 && r[0] != 0) ? 0xc3 : 0xc2);
    } else if (t == "str") {
        encStr(o, r);
    } else if (t == "i8" && sz >= 1) {
        encSint(o, static_cast<std::int8_t>(readLE(r, 1)));
    } else if (t == "i16" && sz >= 2) {
        encSint(o, static_cast<std::int16_t>(readLE(r, 2)));
    } else if (t == "i32" && sz >= 4) {
        encSint(o, static_cast<std::int32_t>(readLE(r, 4)));
    } else if (t == "i64" && sz >= 8) {
        encSint(o, static_cast<std::int64_t>(readLE(r, 8)));
    } else if (t == "u8" && sz >= 1) {
        encUint(o, readLE(r, 1));
    } else if (t == "u16" && sz >= 2) {
        encUint(o, readLE(r, 2));
    } else if (t == "u32" && sz >= 4) {
        encUint(o, readLE(r, 4));
    } else if (t == "u64" && sz >= 8) {
        encUint(o, readLE(r, 8));
    } else if (t == "f32" && sz >= 4) {
        const auto u = static_cast<std::uint32_t>(readLE(r, 4));
        float f = 0;
        std::memcpy(&f, &u, 4);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, 4);
        putU8(o, 0xca);
        putBE(o, bits, 4);
    } else if (t == "f64" && sz >= 8) {
        const std::uint64_t u = readLE(r, 8);
        double d = 0;
        std::memcpy(&d, &u, 8);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &d, 8);
        putU8(o, 0xcb);
        putBE(o, bits, 8);
    } else {
        encBin(o, r);
    }
}

void encodeInto(const IrspValue &v, std::string &o) {
    std::visit(
        [&o](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, IrspNull>) {
                putU8(o, 0xc0);
            } else if constexpr (std::is_same_v<T, IrspSimple>) {
                encStr(o, x.text);
            } else if constexpr (std::is_same_v<T, IrspError>) {
                encMapHdr(o, 2); // {err: CODE, msg: MESSAGE}
                encStr(o, "err");
                encStr(o, x.code);
                encStr(o, "msg");
                encStr(o, x.message);
            } else if constexpr (std::is_same_v<T, IrspInteger>) {
                encSint(o, x.value);
            } else if constexpr (std::is_same_v<T, IrspBulk>) {
                // 编码方向 bulk 仅承载文本（topic 名 / 游标 / PING 回显 / map key）。
                encStr(o, x.data);
            } else if constexpr (std::is_same_v<T, IrspTypedValue>) {
                encTyped(o, x);
            } else if constexpr (std::is_same_v<T, IrspArray>) {
                encArrHdr(o, x.items.size());
                for (const auto &it : x.items) {
                    encodeInto(it, o);
                }
            } else if constexpr (std::is_same_v<T, IrspMap>) {
                encMapHdr(o, x.entries.size());
                for (const auto &[k, val] : x.entries) {
                    encodeInto(k, o);
                    encodeInto(val, o);
                }
            }
        },
        v);
}

// ============================ 解码 ============================

struct PResult {
    CodecStatus status{CodecStatus::Error};
    IrspValue value{IrspNull{}};
    std::size_t pos{0};
};

/// 从大端字节读 n 字节为无符号整数。
std::uint64_t rdBE(std::string_view b, std::size_t pos, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) {
        v = (v << 8) | static_cast<std::uint8_t>(b[pos + i]);
    }
    return v;
}

PResult parseOne(std::string_view b, std::size_t pos) {
    if (pos >= b.size()) {
        return {CodecStatus::Incomplete, IrspNull{}, pos};
    }
    const auto c = static_cast<std::uint8_t>(b[pos]);

    // str/bin：数据从 dataStart 起 len 字节，统一解码为 IrspBulk。
    auto bytes = [&](std::size_t dataStart, std::uint64_t len) -> PResult {
        if (b.size() < dataStart + len) {
            return {CodecStatus::Incomplete, IrspNull{}, pos};
        }
        return {CodecStatus::Ok, makeBulk(std::string(b.substr(dataStart, len))), dataStart + len};
    };
    // float：大端 bits -> 小端原始字节（与 IRSP value 字节约定一致），存入 IrspBulk。
    auto floatBulk = [&](std::size_t dataStart, int n) -> PResult {
        if (b.size() < dataStart + static_cast<std::size_t>(n)) {
            return {CodecStatus::Incomplete, IrspNull{}, pos};
        }
        const std::uint64_t bits = rdBE(b, dataStart, n);
        std::string le(static_cast<std::size_t>(n), '\0');
        for (int i = 0; i < n; ++i) {
            le[i] = static_cast<char>((bits >> (8 * i)) & 0xff);
        }
        return {CodecStatus::Ok, makeBulk(std::move(le)), dataStart + static_cast<std::size_t>(n)};
    };
    auto array = [&](std::size_t hdrEnd, std::uint64_t count) -> PResult {
        IrspArray arr;
        arr.items.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(count, b.size())));
        std::size_t cur = hdrEnd;
        for (std::uint64_t i = 0; i < count; ++i) {
            PResult e = parseOne(b, cur);
            if (e.status != CodecStatus::Ok) {
                return {e.status, IrspNull{}, pos};
            }
            arr.items.push_back(std::move(e.value));
            cur = e.pos;
        }
        return {CodecStatus::Ok, std::move(arr), cur};
    };
    auto map = [&](std::size_t hdrEnd, std::uint64_t count) -> PResult {
        IrspMap m;
        m.entries.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(count, b.size())));
        std::size_t cur = hdrEnd;
        for (std::uint64_t i = 0; i < count; ++i) {
            PResult k = parseOne(b, cur);
            if (k.status != CodecStatus::Ok) {
                return {k.status, IrspNull{}, pos};
            }
            cur = k.pos;
            PResult v = parseOne(b, cur);
            if (v.status != CodecStatus::Ok) {
                return {v.status, IrspNull{}, pos};
            }
            cur = v.pos;
            m.entries.emplace_back(std::move(k.value), std::move(v.value));
        }
        return {CodecStatus::Ok, std::move(m), cur};
    };
    // 读取 n 字节长度前缀（位于 pos+1），不足则 false。
    auto lenPrefix = [&](int n, std::uint64_t &len) -> bool {
        if (b.size() < pos + 1 + static_cast<std::size_t>(n)) {
            return false;
        }
        len = rdBE(b, pos + 1, n);
        return true;
    };

    if (c <= 0x7f) {
        return {CodecStatus::Ok, makeInteger(c), pos + 1}; // positive fixint
    }
    if (c >= 0xe0) {
        return {CodecStatus::Ok, makeInteger(static_cast<std::int8_t>(c)), pos + 1}; // neg fixint
    }
    if (c >= 0x80 && c <= 0x8f) {
        return map(pos + 1, c & 0x0f); // fixmap
    }
    if (c >= 0x90 && c <= 0x9f) {
        return array(pos + 1, c & 0x0f); // fixarray
    }
    if (c >= 0xa0 && c <= 0xbf) {
        return bytes(pos + 1, c & 0x1f); // fixstr
    }

    std::uint64_t len = 0;
    switch (c) {
    case 0xc0:
        return {CodecStatus::Ok, makeNull(), pos + 1};
    case 0xc2:
        return {CodecStatus::Ok, makeInteger(0), pos + 1};
    case 0xc3:
        return {CodecStatus::Ok, makeInteger(1), pos + 1};
    case 0xcc:
        if (b.size() < pos + 2) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int64_t>(rdBE(b, pos + 1, 1))), pos + 2};
    case 0xcd:
        if (b.size() < pos + 3) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int64_t>(rdBE(b, pos + 1, 2))), pos + 3};
    case 0xce:
        if (b.size() < pos + 5) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int64_t>(rdBE(b, pos + 1, 4))), pos + 5};
    case 0xcf:
        if (b.size() < pos + 9) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int64_t>(rdBE(b, pos + 1, 8))), pos + 9};
    case 0xd0:
        if (b.size() < pos + 2) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int8_t>(rdBE(b, pos + 1, 1))), pos + 2};
    case 0xd1:
        if (b.size() < pos + 3) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int16_t>(rdBE(b, pos + 1, 2))), pos + 3};
    case 0xd2:
        if (b.size() < pos + 5) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int32_t>(rdBE(b, pos + 1, 4))), pos + 5};
    case 0xd3:
        if (b.size() < pos + 9) break;
        return {CodecStatus::Ok, makeInteger(static_cast<std::int64_t>(rdBE(b, pos + 1, 8))), pos + 9};
    case 0xca:
        return floatBulk(pos + 1, 4);
    case 0xcb:
        return floatBulk(pos + 1, 8);
    case 0xc4:
        if (!lenPrefix(1, len)) break;
        return bytes(pos + 2, len);
    case 0xc5:
        if (!lenPrefix(2, len)) break;
        return bytes(pos + 3, len);
    case 0xc6:
        if (!lenPrefix(4, len)) break;
        return bytes(pos + 5, len);
    case 0xd9:
        if (!lenPrefix(1, len)) break;
        return bytes(pos + 2, len);
    case 0xda:
        if (!lenPrefix(2, len)) break;
        return bytes(pos + 3, len);
    case 0xdb:
        if (!lenPrefix(4, len)) break;
        return bytes(pos + 5, len);
    case 0xdc:
        if (!lenPrefix(2, len)) break;
        return array(pos + 3, len);
    case 0xdd:
        if (!lenPrefix(4, len)) break;
        return array(pos + 5, len);
    case 0xde:
        if (!lenPrefix(2, len)) break;
        return map(pos + 3, len);
    case 0xdf:
        if (!lenPrefix(4, len)) break;
        return map(pos + 5, len);
    default:
        return {CodecStatus::Error, IrspNull{}, pos}; // ext / 保留字节：不支持
    }
    return {CodecStatus::Incomplete, IrspNull{}, pos};
}

} // namespace

void MsgpackCodec::encode(const IrspValue &value, std::string &out) { encodeInto(value, out); }

std::string MsgpackCodec::encode(const IrspValue &value) {
    std::string out;
    encodeInto(value, out);
    return out;
}

CodecDecode MsgpackCodec::decode(std::string_view buffer) {
    PResult r = parseOne(buffer, 0);
    return CodecDecode{r.status, std::move(r.value), r.pos};
}

// ============================ 跨编码门面 ============================

std::string encodeValue(Encoding enc, const IrspValue &value) {
    return enc == Encoding::Msgpack ? MsgpackCodec::encode(value) : Irsp1Codec::encode(value);
}

CodecDecode decodeValue(Encoding enc, std::string_view buffer) {
    if (enc == Encoding::Msgpack) {
        return MsgpackCodec::decode(buffer);
    }
    Irsp1Codec::DecodeResult r = Irsp1Codec::decode(buffer);
    const CodecStatus st = r.status == Irsp1Codec::Status::Ok          ? CodecStatus::Ok
                           : r.status == Irsp1Codec::Status::Incomplete ? CodecStatus::Incomplete
                                                                        : CodecStatus::Error;
    return CodecDecode{st, std::move(r.value), r.consumed};
}

} // namespace irsp