#include <atomic>
#include <chrono>
#include <stdexcept>
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

    // 任务抛异常被隔离：调度线程存活，抛异常任务自身下周期继续触发，
    // 同时其它正常任务不受影响。
    {
        Scheduler sched;
        std::atomic<int> throwTicks{0};
        std::atomic<int> goodTicks{0};
        sched.start();
        sched.addPeriodicTask("boom", std::chrono::milliseconds(20), [&] {
            throwTicks.fetch_add(1);
            throw std::runtime_error("boom");
        });
        sched.addPeriodicTask("good", std::chrono::milliseconds(20),
                              [&] { goodTicks.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        // 抛异常任务多次重入说明调度线程未因异常终止。
        IR_CHECK(throwTicks.load() >= 3);
        // 正常任务不被同周期的异常任务波及。
        IR_CHECK(goodTicks.load() >= 3);
        sched.stop();
    }

    IR_TEST_REPORT();
}
