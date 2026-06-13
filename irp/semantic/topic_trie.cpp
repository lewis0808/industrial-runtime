#include "semantic/topic_trie.hpp"

#include "semantic/topic.hpp"

namespace irp {

bool TopicTrie::subscribe(std::string_view pattern, SubscriberId id) {
    if (!TopicMatcher::isValidPattern(pattern)) {
        return false;
    }
    Node* cur = &root_;
    for (const auto seg : splitTopic(pattern)) {
        if (seg == "#") {
            if (!cur->hashChild) {
                cur->hashChild = std::make_unique<Node>();
            }
            cur = cur->hashChild.get();
            break;  // '#' 为末段
        }
        auto& child = cur->children[std::string(seg)];
        if (!child) {
            child = std::make_unique<Node>();
        }
        cur = child.get();
    }
    cur->subscribers.insert(id);
    idPatterns_[id].insert(std::string(pattern));
    return true;
}

bool TopicTrie::unsubscribe(std::string_view pattern, SubscriberId id) {
    Node* cur = &root_;
    for (const auto seg : splitTopic(pattern)) {
        if (seg == "#") {
            if (!cur->hashChild) {
                return false;
            }
            cur = cur->hashChild.get();
            break;
        }
        auto it = cur->children.find(std::string(seg));
        if (it == cur->children.end()) {
            return false;
        }
        cur = it->second.get();
    }
    const bool removed = cur->subscribers.erase(id) > 0;
    if (removed) {
        auto it = idPatterns_.find(id);
        if (it != idPatterns_.end()) {
            it->second.erase(std::string(pattern));
            if (it->second.empty()) {
                idPatterns_.erase(it);
            }
        }
    }
    return removed;
}

void TopicTrie::unsubscribeAll(SubscriberId id) {
    auto it = idPatterns_.find(id);
    if (it == idPatterns_.end()) {
        return;
    }
    // 拷贝模式集合，避免在遍历中修改。
    const std::set<std::string> patterns = it->second;
    for (const auto& pattern : patterns) {
        unsubscribe(pattern, id);
    }
}

void TopicTrie::collect(const Node* node, const std::vector<std::string_view>& segs,
                        std::size_t idx, std::set<SubscriberId>& out) {
    // '#' 子节点匹配剩余零或多层。
    if (node->hashChild) {
        out.insert(node->hashChild->subscribers.begin(),
                   node->hashChild->subscribers.end());
    }
    if (idx == segs.size()) {
        out.insert(node->subscribers.begin(), node->subscribers.end());
        return;
    }
    // 字面段精确匹配。
    if (auto it = node->children.find(std::string(segs[idx]));
        it != node->children.end()) {
        collect(it->second.get(), segs, idx + 1, out);
    }
    // '+' 单层通配。
    if (auto it = node->children.find("+"); it != node->children.end()) {
        collect(it->second.get(), segs, idx + 1, out);
    }
}

std::vector<TopicTrie::SubscriberId> TopicTrie::match(std::string_view topic) const {
    std::set<SubscriberId> found;
    if (TopicMatcher::isConcrete(topic)) {
        const auto segs = splitTopic(topic);
        collect(&root_, segs, 0, found);
    }
    return std::vector<SubscriberId>(found.begin(), found.end());
}

std::size_t TopicTrie::size() const noexcept {
    std::size_t total = 0;
    for (const auto& [id, patterns] : idPatterns_) {
        total += patterns.size();
    }
    return total;
}

std::size_t TopicTrie::countFor(SubscriberId id) const {
    auto it = idPatterns_.find(id);
    return it == idPatterns_.end() ? 0 : it->second.size();
}

}  // namespace irp
