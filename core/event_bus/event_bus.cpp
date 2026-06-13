#include "event_bus/event_bus.hpp"

#include <chrono>
#include <utility>

namespace core {

EventBus::EventBus(std::size_t queueCapacity) : queue_(queueCapacity) {}

EventBus::~EventBus() {
    stop();
}

void EventBus::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // 已启动
    }
    worker_ = std::jthread([this](std::stop_token st) { dispatchLoop(st); });
}

void EventBus::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // 未启动或已停止
    }
    worker_.request_stop();
    signal_.release();  // 唤醒可能阻塞在 acquire 的派发线程
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool EventBus::publish(const Event& event) {
    if (!queue_.tryEnqueue(event)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    signal_.release();
    return true;
}

bool EventBus::publish(Event&& event) {
    if (!queue_.tryEnqueue(std::move(event))) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    signal_.release();
    return true;
}

EventBus::SubscriptionId EventBus::subscribe(Handler handler, Filter filter) {
    const SubscriptionId id = nextId_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(subsMutex_);
    subscriptions_.push_back(Subscription{id, std::move(handler), std::move(filter)});
    return id;
}

bool EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(subsMutex_);
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        if (it->id == id) {
            subscriptions_.erase(it);
            return true;
        }
    }
    return false;
}

void EventBus::dispatchLoop(std::stop_token stopToken) {
    using namespace std::chrono_literals;
    Event event;
    while (!stopToken.stop_requested()) {
        // 带超时等待，既避免忙等，也能周期性检查停止请求。
        // 返回值无需关心：超时或被唤醒都会继续抽干队列。
        (void)signal_.try_acquire_for(100ms);
        while (queue_.tryDequeue(event)) {
            deliver(event);
        }
    }
    // 停止前抽干剩余事件，保证不丢已入队事件。
    while (queue_.tryDequeue(event)) {
        deliver(event);
    }
}

void EventBus::deliver(const Event& event) {
    // 拷贝一份订阅快照，缩短持锁时间，避免回调内重入死锁。
    std::vector<Subscription> snapshot;
    {
        std::lock_guard<std::mutex> lock(subsMutex_);
        snapshot = subscriptions_;
    }
    for (const auto& sub : snapshot) {
        if (event.severity < sub.filter.minSeverity) {
            continue;
        }
        if (!sub.filter.category.empty() && sub.filter.category != event.category) {
            continue;
        }
        sub.handler(event);
    }
}

}  // namespace core
