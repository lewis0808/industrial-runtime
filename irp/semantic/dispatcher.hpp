#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "codec/resp_value.hpp"
#include "semantic/tag_source.hpp"
#include "semantic/topic_trie.hpp"

namespace irp {

/// 事件订阅过滤器。
struct EventFilter {
    int minSeverityRank{0}; ///< 0 info / 1 warning / 2 alarm / 3 critical
    std::string category;   ///< 空 = 不限分类
};

/// 单连接会话状态（最小）。订阅集中存于 Dispatcher，按 id 索引。
struct Session {
    std::uint64_t id{0};
    bool hello{false};
};

/// IRP 语义引擎 + 订阅注册表。解析命令、产出回复、维护订阅。
///
/// 不依赖传输/编码具体实现，也不依赖 core（经 TagSource 读数据）。
/// **线程不安全**：上层（server）需串行化对同一 Dispatcher 的访问。
class Dispatcher {
  public:
    explicit Dispatcher(const TagSource &tags) noexcept : tags_(&tags) {}

    /// 处理一个请求，返回回复值。会按需改变 session 与订阅状态。
    [[nodiscard]] RespValue handle(Session &session, const RespValue &request);

    /// 连接关闭时清理其订阅。
    void onSessionClosed(std::uint64_t id);

    // ---- 路由查询（供 server 在推送时使用） ----

    /// 匹配某具体 topic 的 tag 订阅者（WATCH/SUBSCRIBE）。
    [[nodiscard]] std::vector<std::uint64_t> tagSubscribers(std::string_view topic) const {
        return tagSubs_.match(topic);
    }

    /// 匹配某事件的订阅者（按级别/分类过滤）。
    [[nodiscard]] std::vector<std::uint64_t> eventSubscribers(int severityRank,
                                                              const std::string &category) const;

  private:
    const TagSource *tags_;
    TopicTrie tagSubs_;                                        ///< tag 订阅（id = session id）
    std::unordered_map<std::uint64_t, EventFilter> eventSubs_; ///< 事件订阅
};

} // namespace irp
