# EventBus — 无锁队列 + 单派发线程扇出

> `event_bus/event_bus.{hpp,cpp}` + `event_bus/mpmc_queue.hpp` · 库 `core_event_bus` · 依赖 `Threads`

高性能事件总线：生产者经**无锁 MPMC 队列**投递，单一 `jthread` 派发线程出队并扇出给订阅者。
生产与消费解耦——生产者不被慢订阅者拖慢。

## 公开 API

```cpp
explicit EventBus(std::size_t queueCapacity = 8192);   // 容量向上取整为 2 的幂
void start();  void stop();                            // 幂等；析构自动 stop
bool publish(const Event&);  bool publish(Event&&);    // 队列满返回 false 并累加丢弃计数；非阻塞
SubscriptionId subscribe(Handler, Filter = {});        // Handler = void(const Event&)
bool unsubscribe(SubscriptionId);
uint64_t droppedCount() const;                         // 因满被丢弃的累计数
struct Filter { EventSeverity minSeverity; std::string category; };  // category 空=不限
```

## MpmcQueue（Vyukov 有界无锁队列）

- 每个 `Cell` 带一个 `atomic<size_t> sequence` + 数据；入队/出队是 **wait-free 的单次尝试**
  （`compare_exchange_weak` 推进 `enqueuePos_`/`dequeuePos_`），失败即返回，不自旋阻塞。
- 容量构造时 `roundUpPow2`，用 `mask` 取模。`enqueuePos_`/`dequeuePos_` 各自 `alignas(cache line)`
  避免两个游标 false sharing。
- 内存序：读 `sequence` 用 `acquire`，写用 `release`，游标推进用 `relaxed` —— 标准 Vyukov 配方。

## EventBus 派发

- `publish`：`tryEnqueue` 成功后 `signal_.release()` 唤醒派发线程；失败则 `dropped_++` 返回 false。
- `dispatchLoop`：`signal_.try_acquire_for(100ms)` 带超时等待（既避免忙等，也能周期检查
  `stop_token`），醒来后**抽干**队列逐条 `deliver`。停止前再抽干一次，保证不丢已入队事件。
- `deliver`：先在锁内**拷贝一份订阅快照**再解锁遍历，缩短持锁时间并避免回调内重入死锁；
  按 `minSeverity` + `category` 过滤后调用 handler。
- `subscribe/unsubscribe` 频率低，用普通 `std::mutex` 保护订阅表 `vector`。

## 线程与异常语义

- `publish` 可多线程并发（无锁队列）。`deliver` 单线程（仅派发线程），故 handler 间无并发，
  但**一个慢 handler 阻塞其后所有事件**的派发。
- handler 抛异常会逃逸出派发线程 → 终止。当前**未做防护**（见待改善）。

## 待改善

| 项 | 说明 | 影响/方向 |
|----|------|-----------|
| **每事件全量拷贝订阅表** | `deliver` 对每条事件都拷贝整个 `vector<Subscription>`（含 `std::function`）。 | 高事件率下每条一次堆分配 → 改 `shared_ptr<const vector>` 写时复制，派发只取一次引用。 |
| **handler 异常未防护** | 抛异常逃逸派发线程 → `std::terminate`。 | `deliver` 内 `try/catch` 包裹每个 handler 并记日志。 |
| **单派发线程** | 一个慢 handler 拖垮全局派发。 | 可选 worker 池 / 每订阅独立队列；或约定 handler 必须非阻塞。 |
| **过滤能力弱** | 仅 `minSeverity` + 单个精确 `category`。 | 需要 source 过滤、多分类、通配/Topic 过滤。 |
| **丢弃即无声** | 满了只计数，无背压/无溢出策略选择。 | 提供策略：阻塞 / 覆盖最旧 / 扩容；并暴露丢弃速率指标。 |
| **容量不可经 init 配置** | 默认 8192，构造期固定；`RuntimeEngine.init` 注释提到「队列容量」但未接线。 | 把 `queueCapacity` 接到 Config。 |
| **Cell 无缓存行填充** | 相邻 Cell 可能共享缓存行，高竞争下 false sharing。 | 视压测结果决定是否 `alignas` 填充 Cell。 |
