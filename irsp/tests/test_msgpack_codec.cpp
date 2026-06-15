#include <cstdint>
#include <cstring>
#include <string>
#include <variant>

#include "codec/encoding.hpp"
#include "codec/irsp_value.hpp"
#include "codec/msgpack_codec.hpp"
#include "tests/test_util.hpp"

using namespace irsp;

namespace {

/// 把数值的小端原始字节装进 std::string（模拟 TagRecord.value）。
template <typename T> std::string leBytes(T v) {
    std::string s(sizeof(T), '\0');
    std::memcpy(s.data(), &v, sizeof(T));
    return s;
}

std::string enc(const IrspValue &v) { return MsgpackCodec::encode(v); }

/// 期望字节里的 msgpack fixstr（避免十六进制转义紧贴字母的字面量陷阱）。
std::string fixstr(const std::string &s) {
    std::string o(1, static_cast<char>(0xa0 | s.size()));
    o += s;
    return o;
}

} // namespace

int main() {
    // ---- 编码：精确字节 ----
    IR_CHECK(enc(makeNull()) == std::string("\xc0", 1));
    IR_CHECK(enc(makeInteger(5)) == std::string("\x05", 1));   // positive fixint
    IR_CHECK(enc(makeInteger(-1)) == std::string("\xff", 1));  // negative fixint
    IR_CHECK(enc(makeInteger(-32)) == std::string("\xe0", 1)); // negative fixint 下界
    IR_CHECK(enc(makeInteger(200)) == std::string("\xcc\xc8", 2));         // uint8
    IR_CHECK(enc(makeInteger(-100)) == std::string("\xd0\x9c", 2));        // int8
    IR_CHECK(enc(makeBulk("hi")) == std::string("\xa2hi", 3));             // fixstr
    IR_CHECK(enc(makeSimple("OK")) == std::string("\xa2OK", 3));           // Simple 同 str

    // Error -> {err: CODE, msg: MESSAGE}
    IR_CHECK(enc(makeError("ERR", "bad")) ==
             std::string(1, static_cast<char>(0x82)) + fixstr("err") + fixstr("ERR") +
                 fixstr("msg") + fixstr("bad"));

    // 数组 *2 GET a/b
    {
        IrspArray a;
        a.items.push_back(makeBulk("GET"));
        a.items.push_back(makeBulk("a/b"));
        IR_CHECK(enc(a) == std::string("\x92\xa3GET\xa3"
                                       "a/b",
                                       9));
    }
    // map {k:1}
    {
        IrspMap m;
        m.entries.emplace_back(makeBulk("k"), makeInteger(1));
        IR_CHECK(enc(m) == std::string("\x81\xa1k\x01", 4));
    }

    // ---- TypedValue：依 type 编码为原生类型（V2 核心收益）----
    // f64 = 2.0 -> 0xcb + 大端 IEEE754
    IR_CHECK(enc(makeTypedValue("f64", leBytes<double>(2.0))) ==
             std::string("\xcb\x40\x00\x00\x00\x00\x00\x00\x00", 9));
    // i32 = 42 -> fixint
    IR_CHECK(enc(makeTypedValue("i32", leBytes<std::int32_t>(42))) == std::string("\x2a", 1));
    // i32 = -2 -> negative fixint
    IR_CHECK(enc(makeTypedValue("i32", leBytes<std::int32_t>(-2))) == std::string("\xfe", 1));
    // u16 = 300 -> uint16
    IR_CHECK(enc(makeTypedValue("u16", leBytes<std::uint16_t>(300))) ==
             std::string("\xcd\x01\x2c", 3));
    // bool true / false
    IR_CHECK(enc(makeTypedValue("bool", std::string("\x01", 1))) == std::string("\xc3", 1));
    IR_CHECK(enc(makeTypedValue("bool", std::string("\x00", 1))) == std::string("\xc2", 1));
    // str
    IR_CHECK(enc(makeTypedValue("str", "hi")) == std::string("\xa2hi", 3));
    // 未知类型 -> bin 兜底（无损）
    IR_CHECK(enc(makeTypedValue("blob", "ab")) == std::string("\xc4\x02"
                                                              "ab",
                                                              4));

    // ---- 解码：基本类型 ----
    {
        auto r = MsgpackCodec::decode(std::string("\xc0", 1));
        IR_CHECK(r.status == CodecStatus::Ok);
        IR_CHECK(std::holds_alternative<IrspNull>(r.value));
        IR_CHECK_EQ(r.consumed, std::size_t{1});
    }
    {
        auto r = MsgpackCodec::decode(std::string("\xcc\xc8", 2)); // uint8 200
        IR_CHECK(r.status == CodecStatus::Ok);
        IR_CHECK(std::get<IrspInteger>(r.value).value == 200);
    }
    {
        auto r = MsgpackCodec::decode(std::string("\xa2hi", 3)); // fixstr
        IR_CHECK(r.status == CodecStatus::Ok);
        IR_CHECK(std::get<IrspBulk>(r.value).data == "hi");
    }

    // ---- 命令请求往返（array of str/bin -> IrspBulk）----
    {
        IrspArray a;
        a.items.push_back(makeBulk("HELLO"));
        a.items.push_back(makeBulk("1"));
        auto r = MsgpackCodec::decode(enc(a));
        IR_CHECK(r.status == CodecStatus::Ok);
        const auto &arr = std::get<IrspArray>(r.value);
        IR_CHECK_EQ(arr.items.size(), std::size_t{2});
        IR_CHECK(std::get<IrspBulk>(arr.items[0]).data == "HELLO");
        IR_CHECK(std::get<IrspBulk>(arr.items[1]).data == "1");
    }

    // ---- 大整数（ts 纳秒）往返 ----
    {
        const std::int64_t ts = 1749800000000000000LL;
        auto r = MsgpackCodec::decode(enc(makeInteger(ts)));
        IR_CHECK(r.status == CodecStatus::Ok);
        IR_CHECK(std::get<IrspInteger>(r.value).value == ts);
    }

    // ---- map 往返（TagValue 形态，value 原生 f64）----
    {
        IrspMap m;
        m.entries.emplace_back(makeBulk("name"), makeBulk("a/b/c"));
        m.entries.emplace_back(makeBulk("type"), makeBulk("f64"));
        m.entries.emplace_back(makeBulk("ts"), makeInteger(123));
        m.entries.emplace_back(makeBulk("value"), makeTypedValue("f64", leBytes<double>(3.5)));
        auto r = MsgpackCodec::decode(enc(m));
        IR_CHECK(r.status == CodecStatus::Ok);
        const auto &rm = std::get<IrspMap>(r.value);
        IR_CHECK_EQ(rm.entries.size(), std::size_t{4});
        IR_CHECK(std::get<IrspBulk>(rm.entries[0].first).data == "name");
        IR_CHECK(std::get<IrspInteger>(rm.entries[2].second).value == 123);
        // value 解回为原始小端字节（IrspBulk），还原为 double == 3.5
        const auto &vb = std::get<IrspBulk>(rm.entries[3].second).data;
        IR_CHECK_EQ(vb.size(), std::size_t{8});
        double d = 0;
        std::memcpy(&d, vb.data(), 8);
        IR_CHECK(d == 3.5);
    }

    // ---- 不完整 ----
    {
        IR_CHECK(MsgpackCodec::decode(std::string("\xa3hi", 3)).status ==
                 CodecStatus::Incomplete); // fixstr len3 仅 2 字节
        IR_CHECK(MsgpackCodec::decode(std::string("\x92\xa3GET", 5)).status ==
                 CodecStatus::Incomplete); // array2 缺第二个元素
        IR_CHECK(MsgpackCodec::decode(std::string("\xcc", 1)).status ==
                 CodecStatus::Incomplete); // uint8 缺数据
        IR_CHECK(MsgpackCodec::decode(std::string()).status == CodecStatus::Incomplete);
    }

    // ---- 坏帧（保留 / ext 字节不支持）----
    IR_CHECK(MsgpackCodec::decode(std::string("\xc1", 1)).status == CodecStatus::Error);

    // ---- 门面 + 嗅探 ----
    // 嗅探只识别顶层容器（array=请求、map=回复/推送）；裸标量不是合法帧，按 irsp1 处理。
    IR_CHECK(sniffEncoding(enc(makeNull())) == Encoding::Irsp1);        // 0xc0 非容器
    {
        IrspArray a;
        a.items.push_back(makeBulk("HELLO"));
        IR_CHECK(sniffEncoding(enc(a)) == Encoding::Msgpack);          // 0x91 fixarray
    }
    {
        IrspMap m;
        m.entries.emplace_back(makeBulk("k"), makeInteger(1));
        IR_CHECK(sniffEncoding(enc(m)) == Encoding::Msgpack);          // 0x81 fixmap
    }
    IR_CHECK(sniffEncoding("*1\r\n$5\r\nHELLO\r\n") == Encoding::Irsp1); // '*'
    IR_CHECK(sniffEncoding("HELLO 1") == Encoding::Irsp1);              // inline 文本

    IR_CHECK(encodeValue(Encoding::Msgpack, makeInteger(5)) == std::string("\x05", 1));
    IR_CHECK(encodeValue(Encoding::Irsp1, makeInteger(5)) == std::string(":5\r\n"));
    {
        auto r = decodeValue(Encoding::Irsp1, ":42\r\n");
        IR_CHECK(r.status == CodecStatus::Ok);
        IR_CHECK(std::get<IrspInteger>(r.value).value == 42);
    }

    IR_TEST_REPORT();
}
