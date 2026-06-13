#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace core {

/// 周期任务调度器：驱动采集周期等定时任务。
///
/// 单一调度线程（jthread）按最近到期时间可中断休眠；到期任务在调度线程内
/// 同步执行。耗时任务应自行卸载到其它线程，避免阻塞后续调度。
class Scheduler {
  public:
    using TaskId = std::uint64_t;
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;

    Scheduler() = default;
    ~Scheduler();

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

    /// 启动调度线程。重复调用无副作用。
    void start();

    /// 停止调度线程。析构时自动调用。
    void stop();

    /// 注册周期任务，立即纳入调度。返回任务 id。
    /// @param interval 执行周期；首个执行在 now + interval 之后。
    TaskId addPeriodicTask(std::string name, std::chrono::milliseconds interval, Task task);

    /// 移除任务。返回是否确实移除。
    bool removeTask(TaskId id);

    /// 当前任务数量。
    [[nodiscard]] std::size_t taskCount() const;

  private:
    struct Entry {
        TaskId id;
        std::string name;
        std::chrono::milliseconds interval;
        Clock::time_point nextRun;
        Task task;
    };

    void runLoop(std::stop_token stopToken);

    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::unordered_map<TaskId, Entry> tasks_;
    std::uint64_t nextId_{1};
    std::uint64_t version_{0}; ///< 任务表变动版本号，用于唤醒后重算最近到期时间

    std::jthread worker_;
    bool running_{false};
};

} // namespace core
