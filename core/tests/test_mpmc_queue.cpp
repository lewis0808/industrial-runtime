#include <atomic>
#include <thread>
#include <vector>

#include "event_bus/mpmc_queue.hpp"
#include "tests/test_util.hpp"

int main() {
    using namespace core;

    // 基础入队/出队。
    {
        MpmcQueue<int> q(4);
        IR_CHECK(q.capacity() >= 4);
        IR_CHECK(q.tryEnqueue(1));
        IR_CHECK(q.tryEnqueue(2));
        int v = 0;
        IR_CHECK(q.tryDequeue(v));
        IR_CHECK_EQ(v, 1);
        IR_CHECK(q.tryDequeue(v));
        IR_CHECK_EQ(v, 2);
        IR_CHECK(!q.tryDequeue(v)); // 空
    }

    // 队列满返回 false。
    {
        MpmcQueue<int> q(2);
        IR_CHECK(q.tryEnqueue(1));
        IR_CHECK(q.tryEnqueue(2));
        IR_CHECK(!q.tryEnqueue(3)); // 满
    }

    // 多生产者多消费者：总数守恒。
    {
        constexpr int PRODUCERS = 4;
        constexpr int PER_PRODUCER = 10000;
        MpmcQueue<int> q(1024);
        std::atomic<int> produced{0};
        std::atomic<long long> consumedSum{0};
        std::atomic<int> consumedCount{0};
        std::atomic<bool> done{false};

        std::vector<std::thread> threads;
        for (int p = 0; p < PRODUCERS; ++p) {
            threads.emplace_back([&] {
                for (int i = 0; i < PER_PRODUCER; ++i) {
                    while (!q.tryEnqueue(1)) {
                        std::this_thread::yield();
                    }
                    produced.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        std::vector<std::thread> consumers;
        for (int c = 0; c < 2; ++c) {
            consumers.emplace_back([&] {
                int v = 0;
                while (!done.load(std::memory_order_acquire) ||
                       consumedCount.load(std::memory_order_relaxed) < PRODUCERS * PER_PRODUCER) {
                    if (q.tryDequeue(v)) {
                        consumedSum.fetch_add(v, std::memory_order_relaxed);
                        consumedCount.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (auto &t : threads)
            t.join();
        done.store(true, std::memory_order_release);
        for (auto &t : consumers)
            t.join();

        IR_CHECK_EQ(produced.load(), PRODUCERS * PER_PRODUCER);
        IR_CHECK_EQ(consumedSum.load(), static_cast<long long>(PRODUCERS) * PER_PRODUCER);
    }

    IR_TEST_REPORT();
}
