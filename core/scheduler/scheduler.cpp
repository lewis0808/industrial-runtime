#include "scheduler/scheduler.hpp"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

#include "logger/logger.hpp"

namespace core {

Scheduler::~Scheduler() { stop(); }

void Scheduler::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }
        running_ = true;
    }
    worker_ = std::jthread([this](std::stop_token st) { runLoop(st); });
}

void Scheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    worker_.request_stop();
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

Scheduler::TaskId Scheduler::addPeriodicTask(std::string name, std::chrono::milliseconds interval,
                                             Task task) {
    std::lock_guard<std::mutex> lock(mutex_);
    const TaskId id = nextId_++;
    tasks_.emplace(id,
                   Entry{id, std::move(name), interval, Clock::now() + interval, std::move(task)});
    ++version_;
    cv_.notify_all(); // 唤醒调度线程重算最近到期时间
    return id;
}

bool Scheduler::removeTask(TaskId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool removed = tasks_.erase(id) > 0;
    if (removed) {
        ++version_;
        cv_.notify_all();
    }
    return removed;
}

std::size_t Scheduler::taskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void Scheduler::runLoop(std::stop_token stopToken) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopToken.stop_requested()) {
        if (tasks_.empty()) {
            // 无任务时休眠，等待新增任务或停止信号。
            cv_.wait(lock, stopToken, [this] { return !tasks_.empty(); });
            continue;
        }

        // 找出最近到期时间。
        auto soonest = Clock::time_point::max();
        for (const auto &[id, entry] : tasks_) {
            soonest = std::min(soonest, entry.nextRun);
        }

        // 可中断地等待到最近到期时间。任务表变动（version 变化）时提前唤醒重算；
        // 到期超时则继续往下处理；收到停止信号则退出。
        const std::uint64_t observedVersion = version_;
        cv_.wait_until(lock, stopToken, soonest,
                       [this, observedVersion] { return version_ != observedVersion; });
        if (stopToken.stop_requested()) {
            break;
        }
        if (version_ != observedVersion) {
            continue; // 任务表已变，回到循环重算最近到期时间
        }

        // 收集已到期任务，复制其回调后在解锁状态下执行。
        const auto nowTp = Clock::now();
        std::vector<DueTask> due;
        for (auto &[id, entry] : tasks_) {
            if (entry.nextRun <= nowTp) {
                due.push_back(DueTask{id, entry.name, entry.task});
                entry.nextRun = nowTp + entry.interval;
            }
        }

        lock.unlock();
        for (auto &item : due) {
            // 任务回调异常不得逃逸调度线程（否则 std::terminate）：
            // 逐任务隔离，单任务失败仅记日志，不拖垮整个调度器。
            try {
                item.task();
            } catch (const std::exception &ex) {
                IR_LOG_ERROR("scheduler: task '{}' (id={}) threw: {}", item.name, item.id,
                             ex.what());
            } catch (...) {
                IR_LOG_ERROR("scheduler: task '{}' (id={}) threw unknown exception", item.name,
                             item.id);
            }
        }
        lock.lock();
    }
}

} // namespace core
