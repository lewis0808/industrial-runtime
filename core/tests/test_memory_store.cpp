#include <string>

#include "memory_store/memory_store.hpp"
#include "tests/test_util.hpp"

int main() {
    using namespace core;
    MemoryStore store;

    store.set("count", std::int64_t{42});
    store.set("name", std::string{"runtime"});

    auto count = store.getAs<std::int64_t>("count");
    IR_CHECK(count.has_value());
    IR_CHECK_EQ(*count, std::int64_t{42});

    // 类型不匹配返回 nullopt。
    IR_CHECK(!store.getAs<std::string>("count").has_value());

    IR_CHECK(store.exists("name"));
    IR_CHECK_EQ(store.size(), std::size_t{2});

    // 覆盖。
    store.set("count", std::int64_t{100});
    IR_CHECK_EQ(*store.getAs<std::int64_t>("count"), std::int64_t{100});

    // 删除。
    IR_CHECK(store.erase("count"));
    IR_CHECK(!store.exists("count"));
    IR_CHECK(!store.erase("count"));

    IR_CHECK_EQ(store.keys().size(), std::size_t{1});

    store.clear();
    IR_CHECK_EQ(store.size(), std::size_t{0});

    IR_TEST_REPORT();
}
