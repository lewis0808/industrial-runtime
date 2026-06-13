#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/tag_value.hpp"

namespace core {

/// Tag 内存模型：运行时唯一的实时变量存储。
///
/// 采用分片（shard）+ 每片读写锁，写入互不阻塞跨片读取，
/// 适合高频写入 + 高频读取的工业采集场景。线程安全。
class TagEngine {
public:
    /// Tag 变更回调：值首次写入或发生变化时触发。
    using ChangeCallback = std::function<void(const TagValue&)>;

    TagEngine() = default;

    /// 写入或更新一个 Tag。返回值是否相对旧值发生了变化。
    /// 若值发生变化且已注册回调，则同步触发回调。
    bool write(const TagValue& tag);
    bool write(TagValue&& tag);

    /// 批量写入。返回发生变化的 Tag 数量。
    std::size_t writeBatch(const std::vector<TagValue>& tags);

    /// 读取单个 Tag。不存在返回 std::nullopt。
    [[nodiscard]] std::optional<TagValue> read(std::string_view name) const;

    /// 判断 Tag 是否存在。
    [[nodiscard]] bool exists(std::string_view name) const;

    /// 删除 Tag。返回是否确实删除了。
    bool remove(std::string_view name);

    /// 当前 Tag 总数。
    [[nodiscard]] std::size_t size() const;

    /// 遍历所有 Tag 的快照（线程安全，逐片加锁拷贝）。
    [[nodiscard]] std::vector<TagValue> snapshot() const;

    /// 注册全局变更回调。当前实现仅保留最后一次注册的回调。
    void setChangeCallback(ChangeCallback callback);

private:
    static constexpr std::size_t SHARD_COUNT = 16;

    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, TagValue> map;
    };

    [[nodiscard]] Shard& shardFor(std::string_view name);
    [[nodiscard]] const Shard& shardFor(std::string_view name) const;

    /// 真正的写入实现，模板以复用左值/右值路径。
    template <typename TagT>
    bool writeImpl(TagT&& tag);

    std::array<Shard, SHARD_COUNT> shards_;

    mutable std::shared_mutex callbackMutex_;
    ChangeCallback changeCallback_;
};

}  // namespace core