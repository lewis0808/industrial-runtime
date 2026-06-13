#include <cstdint>
#include <vector>

#include "semantic/topic.hpp"
#include "semantic/topic_trie.hpp"
#include "tests/test_util.hpp"

using namespace irp;

namespace {
bool eq(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b) {
    return a == b;  // TopicTrie::match 返回升序去重
}
}  // namespace

int main() {
    // ---- isValidPattern ----
    IR_CHECK(TopicMatcher::isValidPattern("a/b/c"));
    IR_CHECK(TopicMatcher::isValidPattern("#"));
    IR_CHECK(TopicMatcher::isValidPattern("a/#"));
    IR_CHECK(TopicMatcher::isValidPattern("a/+/c"));
    IR_CHECK(TopicMatcher::isValidPattern("+"));
    IR_CHECK(!TopicMatcher::isValidPattern(""));
    IR_CHECK(!TopicMatcher::isValidPattern("a//b"));   // 空段
    IR_CHECK(!TopicMatcher::isValidPattern("a/#/b"));  // # 非末段
    IR_CHECK(!TopicMatcher::isValidPattern("a+"));     // 通配未独占段
    IR_CHECK(!TopicMatcher::isValidPattern("/a"));     // 前导空段

    // ---- isConcrete ----
    IR_CHECK(TopicMatcher::isConcrete("factory1/line1/robot1/temp"));
    IR_CHECK(!TopicMatcher::isConcrete("a/+/c"));
    IR_CHECK(!TopicMatcher::isConcrete("a/#"));
    IR_CHECK(!TopicMatcher::isConcrete(""));

    // ---- matches ----
    IR_CHECK(TopicMatcher::matches("a/b/c", "a/b/c"));
    IR_CHECK(!TopicMatcher::matches("a/b", "a/b/c"));
    IR_CHECK(TopicMatcher::matches("a/+/c", "a/x/c"));
    IR_CHECK(!TopicMatcher::matches("a/+/c", "a/x/y/c"));
    IR_CHECK(TopicMatcher::matches("+/temp", "x/temp"));
    IR_CHECK(!TopicMatcher::matches("+/temp", "x/y/temp"));
    IR_CHECK(TopicMatcher::matches("a/#", "a"));      // # 匹配零层
    IR_CHECK(TopicMatcher::matches("a/#", "a/b"));
    IR_CHECK(TopicMatcher::matches("a/#", "a/b/c"));
    IR_CHECK(!TopicMatcher::matches("a/#", "b/c"));
    IR_CHECK(TopicMatcher::matches("#", "anything/deep/x"));
    IR_CHECK(!TopicMatcher::matches("bad+", "a"));    // 非法 pattern
    IR_CHECK(!TopicMatcher::matches("a/b", "a/+"));   // topic 非具体

    // ---- TopicTrie ----
    TopicTrie trie;
    IR_CHECK(trie.subscribe("factory1/#", 1));
    IR_CHECK(trie.subscribe("factory1/+/temp", 2));
    IR_CHECK(trie.subscribe("factory1/line1/robot1/temp", 3));
    IR_CHECK(trie.subscribe("#", 4));
    IR_CHECK(!trie.subscribe("a/#/b", 5));  // 非法 → false
    IR_CHECK_EQ(trie.size(), std::size_t{4});

    IR_CHECK(eq(trie.match("factory1/line1/robot1/temp"),
                std::vector<std::uint64_t>{1, 3, 4}));
    IR_CHECK(eq(trie.match("factory1/lineA/temp"),
                std::vector<std::uint64_t>{1, 2, 4}));
    IR_CHECK(eq(trie.match("other/x"), std::vector<std::uint64_t>{4}));

    // 退订单个模式。
    IR_CHECK(trie.unsubscribe("factory1/+/temp", 2));
    IR_CHECK(!trie.unsubscribe("factory1/+/temp", 2));  // 已无
    IR_CHECK(eq(trie.match("factory1/lineA/temp"), std::vector<std::uint64_t>{1, 4}));

    // 退订某订阅者全部。
    trie.unsubscribeAll(4);
    IR_CHECK(eq(trie.match("other/x"), std::vector<std::uint64_t>{}));
    IR_CHECK_EQ(trie.size(), std::size_t{2});  // 剩 id1、id3

    // 同一 id 同模式重复订阅不重复计数。
    IR_CHECK(trie.subscribe("a/b", 7));
    IR_CHECK(trie.subscribe("a/b", 7));
    IR_CHECK_EQ(trie.size(), std::size_t{3});

    IR_TEST_REPORT();
}
