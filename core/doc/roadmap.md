# 待改善汇总（Roadmap）

跨模块汇总，按优先级排序。每条都能在对应模块文档找到上下文。优先级仅为建议：
**P0** 健壮性/正确性缺口（应尽快修），**P1** 影响可扩展性/性能的设计债，**P2** 增强项。

## P0 — 健壮性 / 正确性

| # | 模块 | 问题 | 建议 |
|---|------|------|------|
| 1 | [scheduler](modules/scheduler.md) | 任务回调抛异常逃逸调度线程 → `std::terminate`。 | `task()` 外包 `try/catch` 并记日志，单任务失败不拖垮线程。 |
| 2 | [event-bus](modules/event-bus.md) | handler 抛异常逃逸派发线程 → 终止。 | `deliver` 内逐 handler `try/catch`。 |
| 3 | [plugin-system](modules/plugin-system.md) | 插件 `createPlugin/init/start/stop` 跨 DLL 抛异常 → UB。 | 宿主侧调用包异常隔离（或强制 `noexcept` 边界）。 |
| 4 | [data-model](data-model.md) | `DataType` 顺序与 `Variant` 备选顺序的对应是隐式契约，重排即静默错位。 | 加 `static_assert` 锁死每个枚举值↔`index()`。 |

## P1 — 设计债 / 可扩展性

| # | 模块 | 问题 | 建议 |
|---|------|------|------|
| 5 | [plugin-system](modules/plugin-system.md) | **插件生命周期仍是 C++ vtable**（`IPlugin`），要求宿主/插件同一 C++ ABI。 | 生命周期改 C 函数指针 vtable，才能真正任意语言/编译器写插件。（架构级，与 SDK 联动） |
| 6 | [tag-engine](modules/tag-engine.md) | `setChangeCallback` 仅单订阅者；`remove` 不通知。 | 多订阅注册表（返回 id）+ 删除/失效通知。 |
| 7 | [event-bus](modules/event-bus.md) | `deliver` 每条事件全量拷贝订阅表（含 `std::function`）。 | `shared_ptr<const vector>` 写时复制，派发只取引用。 |
| 8 | [tag-engine](modules/tag-engine.md) / [runtime-engine](modules/runtime-engine.md) | 变更回调 / 流 sink 在推送者（插件）线程同步执行，慢消费者阻塞采集。 | 解耦：投递队列 / 专用分发线程。 |
| 9 | [runtime-engine](modules/runtime-engine.md) | `init` 注释称配「队列容量等」，实际只配 Logger；EventBus 容量等构造期写死。 | 把队列容量、分片数等接入 Config，言行一致。 |
| 10 | [plugin-system](modules/plugin-system.md) | 写回路由 O(N) 首匹配，无最长前缀、无冲突检测。 | 最长前缀匹配 + 同前缀冲突告警。 |
| 11 | [event-bus](modules/event-bus.md) | 满即静默丢弃（仅计数），过滤仅 severity+单 category。 | 溢出策略可选 + 丢弃速率指标 + 更强过滤（source/通配）。 |
| 12 | [scheduler](modules/scheduler.md) | 求最近到期 O(N)；周期按实际执行点算会漂移。 | 任务多时改最小堆；按需 `nextRun += interval` + 追赶策略。 |

## P2 — 增强 / 观测 / 生态

| # | 模块 | 方向 |
|---|------|------|
| 13 | [runtime-engine](modules/runtime-engine.md) | 统一可观测面：tag 数 / 事件速率 / 丢弃数 / 插件数等 metrics 快照，经 IRSP 暴露。 |
| 14 | [data-model](data-model.md) | TagValue 增加 quality/质量戳（OPC 风格，IRSP 数据模型已留 map 扩展位）。 |
| 15 | core 整体 | 接入 `stream/` 流处理模块（当前 `pushStream` 仅转交可选 sink，`main` 未注册 sink）。 |
| 16 | [config](modules/config.md) | 热重载 / schema 校验 / env 覆盖（工业现场对热重载有价值）。 |
| 17 | [tag-engine](modules/tag-engine.md) | `string_view` 透明哈希查询，省去临时 `string` 分配。 |
| 18 | [plugin-system](modules/plugin-system.md) | 插件热插拔 / 按 id 卸载 / reload；关键场景探索进程外插件隔离。 |
| 19 | [memory-store](modules/memory-store.md) | 明确用途边界（缓存/状态？）；按需补 TTL/原子操作。 |
| 20 | [logger](modules/logger.md) | 按日期滚动；命名 logger 分模块；可选结构化(JSON) sink。 |

## 已达成（对照基线）

- ✅ 上行数据面贯通：`pushTag/Event/Stream` → RuntimeEngine → 子系统。
- ✅ 写回链路：IRSP `SET` → `writeTag` → PluginHost 前缀路由 → 插件 `onWrite`。
- ✅ 插件自动发现 + 配置分目录（`plugins/` 与 `config/` 分离）。
- ✅ 数据面纯 C ABI（结构/字符串/枚举跨边界安全）。
- ✅ 跨平台 DLL 加载、可中断后台线程、零外部框架单测。
