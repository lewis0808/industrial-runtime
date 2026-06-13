#pragma once

#include <functional>
#include <mutex>

#include "config/config.hpp"
#include "event_bus/event_bus.hpp"
#include "memory_store/memory_store.hpp"
#include "runtime_engine/runtime_api.hpp"
#include "scheduler/scheduler.hpp"
#include "tag_engine/tag_engine.hpp"

namespace core {

/// 运行时引擎：组合各核心子系统并对外提供统一数据中心与 Runtime API。
///
/// 持有 TagEngine / EventBus / MemoryStore / Scheduler，负责其生命周期。
/// 通过组合（而非继承）聚合子系统；仅实现 RuntimeApi 这一对插件可见的接口。
class RuntimeEngine final : public RuntimeApi {
public:
    /// 流数据接收方：core 不解析流，仅转交给注册的 sink（通常由 stream/ 模块提供）。
    using StreamSink = std::function<void(const StreamFrame&)>;

    RuntimeEngine();
    ~RuntimeEngine() override;

    RuntimeEngine(const RuntimeEngine&) = delete;
    RuntimeEngine& operator=(const RuntimeEngine&) = delete;

    /// 按配置初始化（日志级别、队列容量等）。须在 start() 前调用。
    void init(const Config& config);

    /// 启动后台子系统（事件派发线程、调度线程）。
    void start();

    /// 停止全部后台子系统。析构时自动调用。
    void stop();

    // ---- RuntimeApi（插件可见） ----
    bool pushTag(const TagValue& tag) override;
    bool pushEvent(const Event& event) override;
    bool pushStream(const StreamFrame& frame) override;

    // ---- 内部组件访问（供运行时自身与测试使用，不向插件暴露） ----
    [[nodiscard]] TagEngine& tags() noexcept { return tagEngine_; }
    [[nodiscard]] EventBus& events() noexcept { return eventBus_; }
    [[nodiscard]] MemoryStore& store() noexcept { return memoryStore_; }
    [[nodiscard]] Scheduler& scheduler() noexcept { return scheduler_; }

    /// 注册流数据接收方。
    void setStreamSink(StreamSink sink);

private:
    TagEngine tagEngine_;
    EventBus eventBus_;
    MemoryStore memoryStore_;
    Scheduler scheduler_;

    std::mutex streamSinkMutex_;
    StreamSink streamSink_;

    bool started_{false};
};

}  // namespace core
