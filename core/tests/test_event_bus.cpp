#include <atomic>
#include <chrono>
#include <thread>

#include "event_bus/event_bus.hpp"
#include "tests/test_util.hpp"

namespace {

/// 自旋等待直到 pred 为真或超时。
template <typename Pred> bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

} // namespace

int main() {
    using namespace core;

    // 发布/订阅基本投递。
    {
        EventBus bus(64);
        std::atomic<int> received{0};
        bus.subscribe([&](const Event &) { received.fetch_add(1); });
        bus.start();
        for (int i = 0; i < 10; ++i) {
            bus.publish(Event{"src", "cat", "msg", EventSeverity::Info});
        }
        IR_CHECK(waitFor([&] { return received.load() == 10; }, std::chrono::milliseconds(1000)));
        bus.stop();
    }

    // 严重级别过滤。
    {
        EventBus bus(64);
        std::atomic<int> alarms{0};
        bus.subscribe([&](const Event &) { alarms.fetch_add(1); },
                      EventBus::Filter{EventSeverity::Alarm, ""});
        bus.start();
        bus.publish(Event{"s", "c", "info", EventSeverity::Info});     // 过滤掉
        bus.publish(Event{"s", "c", "alarm", EventSeverity::Alarm});   // 收到
        bus.publish(Event{"s", "c", "crit", EventSeverity::Critical}); // 收到
        IR_CHECK(waitFor([&] { return alarms.load() == 2; }, std::chrono::milliseconds(1000)));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        IR_CHECK_EQ(alarms.load(), 2); // 确认 info 未被计入
        bus.stop();
    }

    // 分类过滤 + 退订。
    {
        EventBus bus(64);
        std::atomic<int> got{0};
        auto id = bus.subscribe([&](const Event &) { got.fetch_add(1); },
                                EventBus::Filter{EventSeverity::Info, "temp"});
        bus.start();
        bus.publish(Event{"s", "temp", "m", EventSeverity::Info});  // 收到
        bus.publish(Event{"s", "press", "m", EventSeverity::Info}); // 分类不符
        IR_CHECK(waitFor([&] { return got.load() == 1; }, std::chrono::milliseconds(1000)));
        IR_CHECK(bus.unsubscribe(id));
        IR_CHECK(!bus.unsubscribe(id));
        bus.stop();
    }

    IR_TEST_REPORT();
}
