#include <atomic>
#include <chrono>
#include <thread>

#include "scheduler/scheduler.hpp"
#include "tests/test_util.hpp"

int main() {
    using namespace core;

    // 周期任务按时触发若干次。
    {
        Scheduler sched;
        std::atomic<int> ticks{0};
        sched.start();
        auto id =
            sched.addPeriodicTask("t", std::chrono::milliseconds(20), [&] { ticks.fetch_add(1); });
        IR_CHECK_EQ(sched.taskCount(), std::size_t{1});
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        IR_CHECK(ticks.load() >= 3); // ~7 次，放宽下限避免抖动误判

        // 移除后不再增长。
        IR_CHECK(sched.removeTask(id));
        IR_CHECK_EQ(sched.taskCount(), std::size_t{0});
        const int snapshot = ticks.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        IR_CHECK_EQ(ticks.load(), snapshot);

        sched.stop();
    }

    // 停止后任务不再执行。
    {
        Scheduler sched;
        std::atomic<int> ticks{0};
        sched.start();
        sched.addPeriodicTask("t", std::chrono::milliseconds(10), [&] { ticks.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sched.stop();
        const int afterStop = ticks.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        IR_CHECK_EQ(ticks.load(), afterStop);
    }

    IR_TEST_REPORT();
}
