#include <cstdint>
#include <cstring>
#include <string>
#include <variant>

#include "codec/irsp_value.hpp"
#include "semantic/dispatcher.hpp"
#include "server/core_tag_source.hpp"
#include "tag_engine/tag_engine.hpp"
#include "tests/test_util.hpp"

using namespace irsp;

namespace {
IrspValue cmd(std::vector<std::string> parts) {
    IrspArray a;
    for (auto &p : parts)
        a.items.push_back(makeBulk(std::move(p)));
    return a;
}
const std::string *mapGet(const IrspValue &v, const std::string &key) {
    const auto *m = std::get_if<IrspMap>(&v);
    if (!m)
        return nullptr;
    for (const auto &[k, val] : m->entries) {
        const auto *kb = std::get_if<IrspBulk>(&k);
        if (kb && kb->data == key) {
            const auto *vb = std::get_if<IrspBulk>(&val);
            return vb ? &vb->data : nullptr;
        }
    }
    return nullptr;
}
} // namespace

int main() {
    core::TagEngine engine;
    engine.write(core::TagValue{"a/b", 3.14});
    engine.write(core::TagValue{"a/c", std::int32_t{5}});
    engine.write(core::TagValue{"x/y", std::string{"hi"}});

    CoreTagSource src(engine);

    // 类型标签 + 小端字节往返。
    {
        auto r = src.read("a/b");
        IR_CHECK(r.has_value());
        IR_CHECK(r->type == "f64");
        IR_CHECK(r->ts_ns > 0);
        IR_CHECK_EQ(r->value.size(), std::size_t{8});
        double d = 0;
        std::memcpy(&d, r->value.data(), 8);
        IR_CHECK(d == 3.14);
    }
    {
        auto r = src.read("a/c");
        IR_CHECK(r->type == "i32");
        std::int32_t i = 0;
        std::memcpy(&i, r->value.data(), 4);
        IR_CHECK(i == 5);
    }
    {
        auto r = src.read("x/y");
        IR_CHECK(r->type == "str");
        IR_CHECK(r->value == "hi");
    }
    IR_CHECK(!src.read("no/such").has_value());
    IR_CHECK(src.exists("a/b"));
    IR_CHECK(!src.exists("no/such"));

    // SCAN：全量。
    {
        auto r = src.scan("0", "a/#", 0);
        IR_CHECK_EQ(r.names.size(), std::size_t{2});
        IR_CHECK(r.names[0] == "a/b");
        IR_CHECK(r.names[1] == "a/c");
        IR_CHECK(r.nextCursor == "0");
    }
    // SCAN：分页（count=1，游标续传）。
    {
        auto r1 = src.scan("0", "a/#", 1);
        IR_CHECK_EQ(r1.names.size(), std::size_t{1});
        IR_CHECK(r1.names[0] == "a/b");
        IR_CHECK(r1.nextCursor == "a/b");
        auto r2 = src.scan(r1.nextCursor, "a/#", 1);
        IR_CHECK(r2.names[0] == "a/c");
        IR_CHECK(r2.nextCursor == "0"); // 结束
    }

    // 经 Dispatcher 端到端：HELLO -> GET。
    {
        Dispatcher disp(src);
        Session s{1, false};
        disp.handle(s, cmd({"HELLO", "1"}));
        auto r = disp.handle(s, cmd({"GET", "x/y"}));
        IR_CHECK(mapGet(r, "type") && *mapGet(r, "type") == "str");
        IR_CHECK(mapGet(r, "value") && *mapGet(r, "value") == "hi");
    }

    IR_TEST_REPORT();
}
