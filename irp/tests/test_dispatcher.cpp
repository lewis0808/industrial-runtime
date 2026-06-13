#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "codec/resp_value.hpp"
#include "semantic/dispatcher.hpp"
#include "semantic/tag_source.hpp"
#include "semantic/topic.hpp"
#include "tests/test_util.hpp"

using namespace irp;

namespace {

/// 内存假数据源。
class FakeTags : public TagSource {
  public:
    std::map<std::string, TagRecord> data;

    std::optional<TagRecord> read(const std::string &name) const override {
        auto it = data.find(name);
        if (it == data.end())
            return std::nullopt;
        return it->second;
    }
    bool exists(const std::string &name) const override { return data.count(name) > 0; }
    ScanResult scan(const std::string & /*cursor*/, const std::string &pattern,
                    std::size_t /*count*/) const override {
        ScanResult r;
        r.nextCursor = "0";
        for (const auto &[k, v] : data) {
            if (TopicMatcher::matches(pattern, k))
                r.names.push_back(k);
        }
        return r;
    }
};

RespValue cmd(std::vector<std::string> parts) {
    RespArray a;
    for (auto &p : parts)
        a.items.push_back(makeBulk(std::move(p)));
    return a;
}

const std::string *errCode(const RespValue &v) {
    const auto *e = std::get_if<RespError>(&v);
    return e ? &e->code : nullptr;
}

const std::string *mapGet(const RespValue &v, const std::string &key) {
    const auto *m = std::get_if<RespMap>(&v);
    if (!m)
        return nullptr;
    for (const auto &[k, val] : m->entries) {
        const auto *kb = std::get_if<RespBulk>(&k);
        if (kb && kb->data == key) {
            const auto *vb = std::get_if<RespBulk>(&val);
            return vb ? &vb->data : nullptr;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    FakeTags tags;
    tags.data["factory1/line1/temp"] = TagRecord{"factory1/line1/temp", "f64", 123, "abcdefgh"};
    tags.data["factory1/line2/temp"] = TagRecord{"factory1/line2/temp", "f64", 124, "ABCDEFGH"};

    Dispatcher disp(tags);
    Session s{1, false};

    // 握手前任何命令 -> NOT_READY。
    {
        auto r = disp.handle(s, cmd({"GET", "factory1/line1/temp"}));
        IR_CHECK(errCode(r) && *errCode(r) == "NOT_READY");
    }

    // HELLO 版本协商。
    {
        auto r = disp.handle(s, cmd({"HELLO", "1"}));
        IR_CHECK(s.hello);
        const auto *enc = mapGet(r, "encoding");
        IR_CHECK(enc && *enc == "resp1");
    }
    {
        Session bad{2, false};
        auto r = disp.handle(bad, cmd({"HELLO", "9"}));
        IR_CHECK(errCode(r) && *errCode(r) == "UNSUPPORTED_VERSION");
    }

    // PING。
    IR_CHECK(std::get<RespSimple>(disp.handle(s, cmd({"PING"}))).text == "PONG");
    IR_CHECK(std::get<RespBulk>(disp.handle(s, cmd({"PING", "hi"}))).data == "hi");

    // GET 命中 / 未命中。
    {
        auto r = disp.handle(s, cmd({"GET", "factory1/line1/temp"}));
        const auto *type = mapGet(r, "type");
        IR_CHECK(type && *type == "f64");
        IR_CHECK(mapGet(r, "value") && *mapGet(r, "value") == "abcdefgh");
    }
    IR_CHECK(std::holds_alternative<RespNull>(disp.handle(s, cmd({"GET", "no/such"}))));

    // GET 参数错误。
    IR_CHECK(*errCode(disp.handle(s, cmd({"GET"}))) == "WRONG_ARITY");

    // EXISTS。
    IR_CHECK(std::get<RespInteger>(disp.handle(s, cmd({"EXISTS", "factory1/line1/temp"}))).value ==
             1);
    IR_CHECK(std::get<RespInteger>(disp.handle(s, cmd({"EXISTS", "x"}))).value == 0);

    // MGET。
    {
        auto r = disp.handle(s, cmd({"MGET", "factory1/line1/temp", "no/such"}));
        const auto &a = std::get<RespArray>(r);
        IR_CHECK_EQ(a.items.size(), std::size_t{2});
        IR_CHECK(std::holds_alternative<RespMap>(a.items[0]));
        IR_CHECK(std::holds_alternative<RespNull>(a.items[1]));
    }

    // SCAN。
    {
        auto r = disp.handle(s, cmd({"SCAN", "0", "factory1/#"}));
        const auto &a = std::get<RespArray>(r);
        IR_CHECK(std::get<RespBulk>(a.items[0]).data == "0"); // nextCursor
        IR_CHECK_EQ(std::get<RespArray>(a.items[1]).items.size(), std::size_t{2});
    }

    // WATCH / SUBSCRIBE 与路由。
    {
        auto r = disp.handle(s, cmd({"WATCH", "factory1/line1/temp"}));
        IR_CHECK(std::get<RespInteger>(r).value == 1);
        auto subs = disp.tagSubscribers("factory1/line1/temp");
        IR_CHECK(subs.size() == 1 && subs[0] == 1);
    }
    IR_CHECK(*errCode(disp.handle(s, cmd({"WATCH", "factory1/+/temp"}))) == "ERR"); // 非具体
    {
        auto r = disp.handle(s, cmd({"SUBSCRIBE", "factory1/#"}));
        IR_CHECK(std::get<RespInteger>(r).value == 2); // 已含 WATCH 的 1 个
        auto subs = disp.tagSubscribers("factory1/lineX/foo");
        IR_CHECK(subs.size() == 1 && subs[0] == 1);
    }

    // 事件订阅与路由。
    {
        auto r = disp.handle(s, cmd({"SUBEVENT", "warning", "alarm"}));
        IR_CHECK(std::get<RespInteger>(r).value == 1);
        IR_CHECK(disp.eventSubscribers(1, "alarm").size() == 1); // warning, 分类匹配
        IR_CHECK(disp.eventSubscribers(0, "alarm").empty());     // info < warning
        IR_CHECK(disp.eventSubscribers(3, "other").empty());     // 分类不符
    }

    // 预留命令。
    IR_CHECK(*errCode(disp.handle(s, cmd({"SUBSTREAM", "a/b"}))) == "NOT_IMPLEMENTED");
    IR_CHECK(*errCode(disp.handle(s, cmd({"AUTH", "tok"}))) == "NOT_IMPLEMENTED");

    // 未知命令。
    IR_CHECK(*errCode(disp.handle(s, cmd({"FOObar"}))) == "UNKNOWN_COMMAND");

    // 连接关闭清理订阅。
    disp.onSessionClosed(1);
    IR_CHECK(disp.tagSubscribers("factory1/line1/temp").empty());
    IR_CHECK(disp.eventSubscribers(3, "alarm").empty());

    IR_TEST_REPORT();
}
