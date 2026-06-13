#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"

namespace core {

/// 内存 KV 存储（类 Redis 核心的内存键值表）。
///
/// 与 TagEngine 不同：此处是无时间戳语义的通用键值缓存，
/// 供运行时与模块存放配置缓存、插件状态等。分片 + 读写锁，线程安全。
class MemoryStore {
  public:
    MemoryStore() = default;

    /// 设置键值（覆盖）。
    void set(std::string_view key, Variant value);

    /// 读取值。不存在返回 std::nullopt。
    [[nodiscard]] std::optional<Variant> get(std::string_view key) const;

    /// 按目标类型读取。缺失或类型不匹配返回 std::nullopt。
    template <typename T> [[nodiscard]] std::optional<T> getAs(std::string_view key) const {
        auto v = get(key);
        if (!v) {
            return std::nullopt;
        }
        if (auto *p = std::get_if<T>(&*v)) {
            return *p;
        }
        return std::nullopt;
    }

    /// 键是否存在。
    [[nodiscard]] bool exists(std::string_view key) const;

    /// 删除键。返回是否确实删除。
    bool erase(std::string_view key);

    /// 键值对总数。
    [[nodiscard]] std::size_t size() const;

    /// 列出所有键的快照。
    [[nodiscard]] std::vector<std::string> keys() const;

    /// 清空全部键值。
    void clear();

  private:
    static constexpr std::size_t SHARD_COUNT = 16;

    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, Variant> map;
    };

    [[nodiscard]] Shard &shardFor(std::string_view key);
    [[nodiscard]] const Shard &shardFor(std::string_view key) const;

    std::array<Shard, SHARD_COUNT> shards_;
};

} // namespace core
