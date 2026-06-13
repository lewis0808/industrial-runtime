#pragma once

#include "common/event.hpp"
#include "common/stream.hpp"
#include "common/tag_value.hpp"

namespace core {

/// 插件可见的运行时接口。
///
/// 按项目规范，插件仅允许调用 pushTag/pushEvent/pushStream，
/// 不得访问 TagEngine/MemoryStore/EventBus 等内部组件。
///
/// 注意：跨 DLL 的 C-ABI 封送由 plugin-sdk 负责，core 内部以此抽象接口使用。
class RuntimeApi {
  public:
    virtual ~RuntimeApi() = default;

    /// 推送一个 Tag 值。返回相对旧值是否发生变化。
    virtual bool pushTag(const TagValue &tag) = 0;

    /// 推送一个事件。队列满返回 false。
    virtual bool pushEvent(const Event &event) = 0;

    /// 推送一帧流数据。无流接收方时返回 false。
    virtual bool pushStream(const StreamFrame &frame) = 0;
};

} // namespace core
