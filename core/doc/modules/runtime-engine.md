# RuntimeEngine — 编排层 + RuntimeApi

> `runtime_engine/runtime_engine.{hpp,cpp}` + `runtime_engine/runtime_api.hpp`
> 库 `core_runtime_engine` · 依赖全部子系统

运行时的**编排层**：以**组合（非继承）**持有 TagEngine / EventBus / MemoryStore / Scheduler，
管理其生命周期，并实现 `RuntimeApi`——插件唯一可见的入口。

## RuntimeApi（插件可见的抽象接口）

```cpp
class RuntimeApi {
  virtual bool pushTag(const TagValue&)   = 0;   // 返回相对旧值是否变化
  virtual bool pushEvent(const Event&)    = 0;   // 队列满返回 false
  virtual bool pushStream(const StreamFrame&) = 0; // 无 sink 返回 false
};
```

> 这是「插件仅允许调用 `pushTag/pushEvent/pushStream`」铁律的代码化身。跨 DLL 的 C-ABI 封送
> 由 PluginHost/plugin-sdk 负责，core 内部以此**抽象接口**使用，不感知 C 结构。

## RuntimeEngine

```cpp
void init(const Config&);     // 须在 start() 前；当前仅装配 Logger
void start();  void stop();   // 启停 EventBus + Scheduler 后台线程；幂等；析构自动 stop

// RuntimeApi 实现
bool pushTag(...)    → tagEngine_.write
bool pushEvent(...)  → eventBus_.publish
bool pushStream(...) → streamSink_（无则 false）

// 内部访问（供运行时自身/测试，不向插件暴露）
TagEngine& tags();  EventBus& events();  MemoryStore& store();  Scheduler& scheduler();

// 装配点
void setStreamSink(StreamSink);          // 流接收方（接 stream/ 模块）
void setWriteHandler(WriteHandler);      // 写回出口（接 PluginHost）
bool writeTag(const TagValue&);          // 下发写回；无处理器/无人受理返回 false
```

## 设计要点

- **组合而非继承**：子系统是成员，生命周期随 RuntimeEngine。`start/stop` 按
  `eventBus → scheduler`（启）/ `scheduler → eventBus`（停）顺序。
- **`tags()/events()/...` 内部访问**：供 IRSP server、测试、`main` 的演示任务使用；**不**经
  RuntimeApi 暴露给插件，保持插件只能 `push*`。
- **写回链路**：`setWriteHandler` 注册的处理器（`main` 里接到 `PluginHost::write`）由
  IRSP `SET` 经 `writeTag` 触发；处理器在锁内拷出后于锁外调用，避免回调内重入死锁。
- **流转发**：`pushStream` 持 `streamSinkMutex_` 调用 sink；core 不解析帧内容。

## 待改善

| 项 | 说明 | 方向 |
|----|------|------|
| **init 未接队列容量等** | 注释称「日志级别、队列容量等」，实际只配 Logger；EventBus 容量构造期固定 8192。 | 把 EventBus 容量、分片数等接入 Config，言行一致。 |
| **pushStream 持锁调 sink** | sink 慢会阻塞推送插件线程。 | 同 TagEngine 回调：考虑解耦/异步。 |
| **单 sink / 单 writeHandler** | 流接收方与写回出口各只一个。 | 多下游场景需多路注册。 |
| **无可观测面** | 无统一 metrics/health（EventBus 丢弃数等未汇总暴露）。 | 增加运行时指标快照（tag 数、事件速率、丢弃数、插件数…），可经 IRSP 暴露。 |
| **start/stop 无并发保护** | `started_` 是裸 bool，假定单线程启停（当前由 `main` 保证）。 | 如需并发启停可加原子/锁。 |
