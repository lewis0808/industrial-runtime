#pragma once

#include "irplugin/plugin_abi.h"
#include "runtime_engine/runtime_api.hpp"

namespace core {

/// 把 core 的 RuntimeApi 暴露为插件可用的 C-ABI 宿主接口。
///
/// 持有一个 IrPluginHostApi，其函数指针经静态 thunk 将 C 结构转换为 core 类型后
/// 转发给 RuntimeApi。abi() 返回的指针在本对象存活期间有效。
class PluginHost {
  public:
    explicit PluginHost(RuntimeApi &api) noexcept;

    PluginHost(const PluginHost &) = delete;
    PluginHost &operator=(const PluginHost &) = delete;

    [[nodiscard]] const IrPluginHostApi *abi() const noexcept { return &abi_; }

  private:
    static int pushTagThunk(void *ctx, const IrPluginTagValue *tag);
    static int pushEventThunk(void *ctx, const IrPluginEvent *event);
    static int pushStreamThunk(void *ctx, const IrPluginStreamFrame *frame);

    RuntimeApi *api_;
    IrPluginHostApi abi_;
};

} // namespace core
