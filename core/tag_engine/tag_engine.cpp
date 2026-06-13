#include "tag_engine/tag_engine.hpp"

#include <utility>

namespace core {

namespace {

/// 判断两个 Variant 是否相等（类型与值均相同）。
bool valuesEqual(const Variant& a, const Variant& b) noexcept {
    return a == b;
}

}  // namespace

TagEngine::Shard& TagEngine::shardFor(std::string_view name) {
    const std::size_t h = std::hash<std::string_view>{}(name);
    return shards_[h & (SHARD_COUNT - 1)];
}

const TagEngine::Shard& TagEngine::shardFor(std::string_view name) const {
    const std::size_t h = std::hash<std::string_view>{}(name);
    return shards_[h & (SHARD_COUNT - 1)];
}

template <typename TagT>
bool TagEngine::writeImpl(TagT&& tag) {
    Shard& shard = shardFor(tag.name);
    std::unique_lock lock(shard.mutex);
    auto it = shard.map.find(tag.name);
    if (it != shard.map.end()) {
        const bool changed = !valuesEqual(it->second.value, tag.value);
        it->second = std::forward<TagT>(tag);
        return changed;
    }
    // 先固化 key，避免与 std::forward(tag) 的实参求值顺序未定义导致 key 被 move 置空。
    std::string key = tag.name;
    shard.map.emplace(std::move(key), std::forward<TagT>(tag));
    return true;  // 新增视为变化
}

bool TagEngine::write(const TagValue& tag) {
    const bool changed = writeImpl(tag);
    if (changed) {
        std::shared_lock cbLock(callbackMutex_);
        if (changeCallback_) {
            changeCallback_(tag);
        }
    }
    return changed;
}

bool TagEngine::write(TagValue&& tag) {
    // 变更回调需要 tag 内容，故先取出名称用于回调后读取。
    std::string name = tag.name;
    const bool changed = writeImpl(std::move(tag));
    if (changed) {
        std::shared_lock cbLock(callbackMutex_);
        if (changeCallback_) {
            if (auto stored = read(name)) {
                changeCallback_(*stored);
            }
        }
    }
    return changed;
}

std::size_t TagEngine::writeBatch(const std::vector<TagValue>& tags) {
    std::size_t changedCount = 0;
    for (const auto& tag : tags) {
        if (write(tag)) {
            ++changedCount;
        }
    }
    return changedCount;
}

std::optional<TagValue> TagEngine::read(std::string_view name) const {
    if (name.empty()) {
        return std::nullopt;
    }
    const Shard& shard = shardFor(name);
    std::shared_lock lock(shard.mutex);
    auto it = shard.map.find(std::string(name));
    if (it == shard.map.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool TagEngine::exists(std::string_view name) const {
    const Shard& shard = shardFor(name);
    std::shared_lock lock(shard.mutex);
    return shard.map.find(std::string(name)) != shard.map.end();
}

bool TagEngine::remove(std::string_view name) {
    Shard& shard = shardFor(name);
    std::unique_lock lock(shard.mutex);
    return shard.map.erase(std::string(name)) > 0;
}

std::size_t TagEngine::size() const {
    std::size_t total = 0;
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.map.size();
    }
    return total;
}

std::vector<TagValue> TagEngine::snapshot() const {
    std::vector<TagValue> result;
    result.reserve(size());
    for (const auto& shard : shards_) {
        std::shared_lock lock(shard.mutex);
        for (const auto& [name, tag] : shard.map) {
            result.push_back(tag);
        }
    }
    return result;
}

void TagEngine::setChangeCallback(ChangeCallback callback) {
    std::unique_lock lock(callbackMutex_);
    changeCallback_ = std::move(callback);
}

}  // namespace core