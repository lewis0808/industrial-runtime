#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

#include "common/event.hpp"
#include "event_bus/mpmc_queue.hpp"

namespace core {

/// 高性能事件总线。
///
/// 生产者通过无锁 MPMC 队列投递事件，单一派发线程（jthread）出队并
/// 扇出至所有订阅者。subscribe/unsubscribe 频率低，使用互斥保护订阅表。
class EventBus {
  public:
    using Handler = std::function<void(const Event &)>;
    using SubscriptionId = std::uint64_t;

    /// 订阅过滤器：按最小严重级别 + 可选分类过滤。
    struct Filter {
        EventSeverity minSeverity{EventSeverity::Info};
        std::string category; ///< 空表示不限分类
    };

    /// @param queueCapacity 队列容量（向上取整为 2 的幂）。
    explicit EventBus(std::size_t queueCapacity = 8192);
    ~EventBus();

    EventBus(const EventBus &) = delete;
    EventBus &operator=(const EventBus &) = delete;

    /// 启动派发线程。重复调用无副作用。
    void start();

    /// 停止派发线程并处理完剩余事件。析构时自动调用。
    void stop();

    /// 发布事件。队列满返回 false（并累加丢弃计数）。非阻塞、线程安全。
    bool publish(const Event &event);
    bool publish(Event &&event);

    /// 订阅事件，返回可用于退订的 id。
    SubscriptionId subscribe(Handler handler, Filter filter);

    /// 订阅事件（不限严重级别与分类），返回可用于退订的 id。
    SubscriptionId subscribe(Handler handler);

    /// 退订。返回是否确实移除。
    bool unsubscribe(SubscriptionId id);

    /// 因队列满而被丢弃的事件累计数。
    [[nodiscard]] std::uint64_t droppedCount() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    struct Subscription {
        SubscriptionId id;
        Handler handler;
        Filter filter;
    };

    void dispatchLoop(std::stop_token stopToken);
    void deliver(const Event &event);

    MpmcQueue<Event> queue_;
    std::counting_semaphore<> signal_{0}; ///< 入队唤醒信号，避免派发线程忙等
    std::atomic<std::uint64_t> dropped_{0};

    std::mutex subsMutex_;
    std::vector<Subscription> subscriptions_;
    std::atomic<SubscriptionId> nextId_{1};

    std::jthread worker_;
    std::atomic<bool> running_{false};
};

} // namespace core