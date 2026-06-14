# Scheduler — 可中断周期调度

> `scheduler/scheduler.{hpp,cpp}` · 库 `core_scheduler` · 依赖 `Threads`

单一调度线程驱动周期任务（采集周期、心跳等）。用 `steady_clock`（单调时钟，不受墙钟跳变影响）。

## 公开 API

```cpp
void start();  void stop();                              // 幂等；析构自动 stop
TaskId addPeriodicTask(std::string name, std::chrono::milliseconds interval, Task task);
                                                          // 首次执行在 now + interval；返回 id
bool removeTask(TaskId);
std::size_t taskCount() const;
```

## 设计要点

- **单 `jthread` + `condition_variable_any` + `stop_token`**：`runLoop` 持锁，每轮：
  1. 无任务 → `cv_.wait`（带 `stop_token`）休眠至有新任务或停止；
  2. 扫描所有任务求**最近到期时间** `soonest`；
  3. `cv_.wait_until(lock, stop_token, soonest, pred)` 可中断等待——
     谓词 `version_ != observed` 用于**任务表变动时提前唤醒重算**；
  4. 收集已到期任务，**复制其回调后解锁执行**，执行完再加锁，下一轮重算。
- **版本号唤醒**：`addPeriodicTask/removeTask` 改表后 `++version_` 并 `notify_all`，
  让正在 `wait_until` 的调度线程立刻醒来按新表重算 `soonest`，避免「先睡到旧到期时间才发现表变了」。
- **解锁执行**：到期任务在**未持锁**状态下执行，故任务内可安全地再注册/移除任务，
  且不会在任务执行期间阻塞 `addPeriodicTask`。

## 线程与异常语义

- API 线程安全。**任务在调度线程内同步执行** → 耗时任务必须自行卸载到其它线程，
  否则阻塞后续所有任务的调度。
- ⚠️ **任务回调无异常防护**：`task()` 直接调用，若抛异常会逃逸 `runLoop` → 调度线程 →
  `std::terminate`。这是当前最该修的健壮性缺口。

## 待改善

| 项 | 说明 | 优先级/方向 |
|----|------|-------------|
| **任务异常未捕获** | 抛异常即终止进程。 | **高**：`task()` 外包 `try/catch` 记日志，单任务失败不拖垮调度线程。 |
| **求最近到期 O(N)** | 每轮线性扫全部任务。 | 任务量大时改最小堆 / 有序结构（`multimap<time_point,...>`）。 |
| **周期漂移** | `nextRun = now + interval`（按实际执行点算，非按计划点）。 | 慢 tick 会累积漂移；需要时改 `nextRun += interval` 并做追赶/合并策略。 |
| **单线程串行** | 长任务阻塞其余任务（已在注释声明）。 | 可选 worker 池执行到期任务。 |
| **功能单一** | 只有固定周期任务。 | 视需补：一次性任务、立即执行、cron 表达式、jitter。 |
