#include <string>
#include <variant>

#include "codec/irsp1_codec.hpp"
#include "codec/irsp_value.hpp"
#include "tests/test_util.hpp"

using namespace irsp;
using Status = Irsp1Codec::Status;

int main() {
    // ---- 编码：精确字节 ----
    IR_CHECK(Irsp1Codec::encode(makeSimple("OK")) == std::string("+OK\r\n"));
    IR_CHECK(Irsp1Codec::encode(makeError("ERR", "bad")) == std::string("-ERR bad\r\n"));
    IR_CHECK(Irsp1Codec::encode(makeError("NOT_READY", "")) == std::string("-NOT_READY\r\n"));
    IR_CHECK(Irsp1Codec::encode(makeInteger(5)) == std::string(":5\r\n"));
    IR_CHECK(Irsp1Codec::encode(makeInteger(-1)) == std::string(":-1\r\n"));
    IR_CHECK(Irsp1Codec::encode(makeBulk("hi")) == std::string("$2\r\nhi\r\n"));
    IR_CHECK(Irsp1Codec::encode(makeNull()) == std::string("$-1\r\n"));

    // 数组：*2 GET a/b
    {
        IrspArray arr;
        arr.items.push_back(makeBulk("GET"));
        arr.items.push_back(makeBulk("a/b"));
        IR_CHECK(Irsp1Codec::encode(arr) == std::string("*2\r\n$3\r\nGET\r\n$3\r\na/b\r\n"));
    }

    // map：%1 k -> 1
    {
        IrspMap m;
        m.entries.emplace_back(makeBulk("k"), makeInteger(1));
        IR_CHECK(Irsp1Codec::encode(m) == std::string("%1\r\n$1\r\nk\r\n:1\r\n"));
    }

    // ---- 解码：基本类型 ----
    {
        auto r = Irsp1Codec::decode("+PONG\r\n");
        IR_CHECK(r.status == Status::Ok);
        IR_CHECK(std::holds_alternative<IrspSimple>(r.value));
        IR_CHECK(std::get<IrspSimple>(r.value).text == "PONG");
        IR_CHECK_EQ(r.consumed, std::size_t{7});
    }
    {
        auto r = Irsp1Codec::decode(":42\r\n");
        IR_CHECK(r.status == Status::Ok);
        IR_CHECK(std::get<IrspInteger>(r.value).value == 42);
    }
    {
        auto r = Irsp1Codec::decode("-WRONG_ARITY too many\r\n");
        IR_CHECK(r.status == Status::Ok);
        const auto &e = std::get<IrspError>(r.value);
        IR_CHECK(e.code == "WRONG_ARITY");
        IR_CHECK(e.message == "too many");
    }
    {
        auto r = Irsp1Codec::decode("$-1\r\n");
        IR_CHECK(r.status == Status::Ok);
        IR_CHECK(std::holds_alternative<IrspNull>(r.value));
    }

    // ---- 二进制安全 bulk（含 CRLF 与 NUL）----
    {
        std::string payload = std::string("a\r\nb\0c", 6);
        std::string wire = Irsp1Codec::encode(makeBulk(payload));
        auto r = Irsp1Codec::decode(wire);
        IR_CHECK(r.status == Status::Ok);
        IR_CHECK(std::get<IrspBulk>(r.value).data == payload);
        IR_CHECK_EQ(r.consumed, wire.size());
    }

    // ---- 数组往返 ----
    {
        IrspArray arr;
        arr.items.push_back(makeBulk("MGET"));
        arr.items.push_back(makeBulk("x/y"));
        arr.items.push_back(makeNull());
        auto r = Irsp1Codec::decode(Irsp1Codec::encode(arr));
        IR_CHECK(r.status == Status::Ok);
        const auto &a = std::get<IrspArray>(r.value);
        IR_CHECK_EQ(a.items.size(), std::size_t{3});
        IR_CHECK(std::get<IrspBulk>(a.items[0]).data == "MGET");
        IR_CHECK(std::holds_alternative<IrspNull>(a.items[2]));
    }

    // ---- map 往返（TagValue 形态）----
    {
        IrspMap m;
        m.entries.emplace_back(makeBulk("name"), makeBulk("a/b/c"));
        m.entries.emplace_back(makeBulk("type"), makeBulk("f64"));
        m.entries.emplace_back(makeBulk("ts"), makeInteger(1749800000000000000LL));
        m.entries.emplace_back(makeBulk("value"),
                               makeBulk(std::string("\x00\x00\x00\x00\x00\x00\x00\x40", 8)));
        auto r = Irsp1Codec::decode(Irsp1Codec::encode(m));
        IR_CHECK(r.status == Status::Ok);
        const auto &rm = std::get<IrspMap>(r.value);
        IR_CHECK_EQ(rm.entries.size(), std::size_t{4});
        IR_CHECK(std::get<IrspBulk>(rm.entries[0].first).data == "name");
        IR_CHECK(std::get<IrspInteger>(rm.entries[2].second).value == 1749800000000000000LL);
        IR_CHECK(std::get<IrspBulk>(rm.entries[3].second).data.size() == 8);
    }

    // ---- 不完整 ----
    {
        IR_CHECK(Irsp1Codec::decode("$4\r\nab").status == Status::Incomplete); // 数据不足
        IR_CHECK(Irsp1Codec::decode("*2\r\n$3\r\nGET\r\n").status ==
                 Status::Incomplete);                                     // 缺第二个元素
        IR_CHECK(Irsp1Codec::decode("+OK").status == Status::Incomplete); // 无 CRLF
    }

    // ---- 坏帧 ----
    {
        IR_CHECK(Irsp1Codec::decode(":xy\r\n").status == Status::Error);  // 非整数
        IR_CHECK(Irsp1Codec::decode("?bad\r\n").status == Status::Error); // 未知类型
    }

    // ---- inline 命令（调试用）----
    {
        const auto v = Irsp1Codec::decodeInline("HELLO 1");
        const auto &a = std::get<IrspArray>(v);
        IR_CHECK_EQ(a.items.size(), std::size_t{2});
        IR_CHECK(std::get<IrspBulk>(a.items[0]).data == "HELLO");
        IR_CHECK(std::get<IrspBulk>(a.items[1]).data == "1");

        // 多空白/制表/前后空格被规整。
        const auto v2 = Irsp1Codec::decodeInline("  GET   system/heartbeat \t");
        const auto &a2 = std::get<IrspArray>(v2);
        IR_CHECK_EQ(a2.items.size(), std::size_t{2});
        IR_CHECK(std::get<IrspBulk>(a2.items[1]).data == "system/heartbeat");

        // 空行 -> 空数组。
        IR_CHECK(std::get<IrspArray>(Irsp1Codec::decodeInline("   ")).items.empty());
    }

    IR_TEST_REPORT();
}
