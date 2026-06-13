#include <atomic>
#include <string>

#include "tag_engine/tag_engine.hpp"
#include "tests/test_util.hpp"

int main() {
    using namespace core;
    TagEngine engine;

    // 写入与读取。
    IR_CHECK(engine.write(TagValue{"a", std::int32_t{1}}));  // 新增 -> true
    auto v = engine.read("a");
    IR_CHECK(v.has_value());
    IR_CHECK_EQ(std::get<std::int32_t>(v->value), 1);
    IR_CHECK_EQ(engine.size(), std::size_t{1});

    // 相同值写入不算变化。
    IR_CHECK(!engine.write(TagValue{"a", std::int32_t{1}}));
    // 不同值写入算变化。
    IR_CHECK(engine.write(TagValue{"a", std::int32_t{2}}));
    IR_CHECK_EQ(std::get<std::int32_t>(engine.read("a")->value), 2);

    // 不存在与删除。
    IR_CHECK(!engine.read("missing").has_value());
    IR_CHECK(engine.exists("a"));
    IR_CHECK(engine.remove("a"));
    IR_CHECK(!engine.exists("a"));
    IR_CHECK(!engine.remove("a"));

    // 变更回调。
    std::atomic<int> hits{0};
    engine.setChangeCallback([&](const TagValue& t) {
        if (t.name == "cb") hits.fetch_add(1);
    });
    engine.write(TagValue{"cb", std::int32_t{1}});  // 变化 -> 回调
    engine.write(TagValue{"cb", std::int32_t{1}});  // 无变化 -> 不回调
    engine.write(TagValue{"cb", std::int32_t{2}});  // 变化 -> 回调
    IR_CHECK_EQ(hits.load(), 2);

    // 批量写入。
    std::vector<TagValue> batch{TagValue{"x", std::int32_t{1}},
                                TagValue{"y", std::int32_t{2}}};
    IR_CHECK_EQ(engine.writeBatch(batch), std::size_t{2});

    IR_TEST_REPORT();
}
