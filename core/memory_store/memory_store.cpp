#include "memory_store/memory_store.hpp"

#include <functional>
#include <utility>

namespace core {

MemoryStore::Shard &MemoryStore::shardFor(std::string_view key) {
    const std::size_t h = std::hash<std::string_view>{}(key);
    return shards_[h & (SHARD_COUNT - 1)];
}

const MemoryStore::Shard &MemoryStore::shardFor(std::string_view key) const {
    const std::size_t h = std::hash<std::string_view>{}(key);
    return shards_[h & (SHARD_COUNT - 1)];
}

void MemoryStore::set(std::string_view key, Variant value) {
    Shard &shard = shardFor(key);
    std::unique_lock lock(shard.mutex);
    shard.map[std::string(key)] = std::move(value);
}

std::optional<Variant> MemoryStore::get(std::string_view key) const {
    const Shard &shard = shardFor(key);
    std::shared_lock lock(shard.mutex);
    auto it = shard.map.find(std::string(key));
    if (it == shard.map.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool MemoryStore::exists(std::string_view key) const {
    const Shard &shard = shardFor(key);
    std::shared_lock lock(shard.mutex);
    return shard.map.find(std::string(key)) != shard.map.end();
}

bool MemoryStore::erase(std::string_view key) {
    Shard &shard = shardFor(key);
    std::unique_lock lock(shard.mutex);
    return shard.map.erase(std::string(key)) > 0;
}

std::size_t MemoryStore::size() const {
    std::size_t total = 0;
    for (const auto &shard : shards_) {
        std::shared_lock lock(shard.mutex);
        total += shard.map.size();
    }
    return total;
}

std::vector<std::string> MemoryStore::keys() const {
    std::vector<std::string> result;
    result.reserve(size());
    for (const auto &shard : shards_) {
        std::shared_lock lock(shard.mutex);
        for (const auto &[key, value] : shard.map) {
            result.push_back(key);
        }
    }
    return result;
}

void MemoryStore::clear() {
    for (auto &shard : shards_) {
        std::unique_lock lock(shard.mutex);
        shard.map.clear();
    }
}

} // namespace core
