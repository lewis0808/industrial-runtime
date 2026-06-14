#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace irp {

/// 订阅 Trie：按 topic 层级组织订阅模式，支持具体 topic 的高效匹配查找。
///
/// 匹配复杂度与层级深度相关而非 Tag 总数，适配十万~百万级 Tag。
/// 通配符语义见 TopicMatcher（`+` 单层、`#` 多层末段）。线程不安全，由上层串行使用。
class TopicTrie {
  public:
    using SubscriberId = std::uint64_t;

    /// 订阅一个模式。pattern 非法返回 false。
    bool subscribe(std::string_view pattern, SubscriberId id);

    /// 取消某订阅者的某模式。返回是否确实移除。
    bool unsubscribe(std::string_view pattern, SubscriberId id);

    /// 取消某订阅者的所有模式（连接断开时调用）。
    void unsubscribeAll(SubscriberId id);

    /// 返回所有匹配该具体 topic 的订阅者 id（去重、升序）。
    [[nodiscard]] std::vector<SubscriberId> match(std::string_view topic) const;

    /// 当前 (模式, 订阅者) 注册总数。
    [[nodiscard]] std::size_t size() const noexcept;

    /// 某订阅者当前注册的模式数。
    [[nodiscard]] std::size_t countFor(SubscriberId id) const;

  private:
    struct Node {
        std::unordered_map<std::string, std::unique_ptr<Node>> children; ///< 字面段与 "+"
        std::unique_ptr<Node> hashChild;                                 ///< "#"
        std::set<SubscriberId> subscribers;                              ///< 终止于此的订阅者
    };

    static void collect(const Node *node, const std::vector<std::string_view> &segs,
                        std::size_t idx, std::set<SubscriberId> &out);

    Node root_;
    std::unordered_map<SubscriberId, std::set<std::string>> idPatterns_; ///< 反向索引
};

} // namespace irp
