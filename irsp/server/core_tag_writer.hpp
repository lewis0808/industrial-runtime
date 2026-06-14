#pragma once

#include <string>

#include "semantic/tag_writer.hpp"

namespace core {
class RuntimeEngine;
}

namespace irp {

/// 把 IRP 语义层的写回接到 core：(类型标签 + 字节) -> core::TagValue -> RuntimeEngine.writeTag。
///
/// 与 CoreTagSource（读）对称。实际路由到哪个插件由 RuntimeEngine 的写回处理器决定
/// （通常接到 PluginHost，按 topic 前缀归属）。
class CoreTagWriter final : public TagWriter {
  public:
    explicit CoreTagWriter(core::RuntimeEngine &runtime) noexcept : runtime_(&runtime) {}

    [[nodiscard]] WriteResult write(const std::string &name, const std::string &type,
                                    const std::string &value) override;

  private:
    core::RuntimeEngine *runtime_;
};

} // namespace irp
