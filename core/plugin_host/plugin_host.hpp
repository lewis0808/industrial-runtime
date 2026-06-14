#pragma once

#include <string>
#include <vector>

#include "common/tag_value.hpp"
#include "irplugin/plugin_abi.h"
#include "runtime_engine/runtime_api.hpp"

namespace core {

/// 把 core 的 RuntimeApi 暴露为插件可用的 C-ABI 宿主接口。
///
/// 持有一个 IrPluginHostApi，其函数指针经静态 thunk 将 C 结构转换为 core 类型后
/// 转发给 RuntimeApi。abi() 返回的指针在本对象存活期间有效。
///
/// 同时维护插件注册的写回处理器（按 topic 前缀归属），供 write() 把应用 SET
/// 路由到对应插件。
class PluginHost {
  public:
    explicit PluginHost(RuntimeApi &api) noexcept;

    PluginHost(const PluginHost &) = delete;
    PluginHost &operator=(const PluginHost &) = delete;

    [[nodiscard]] const IrPluginHostApi *abi() const noexcept { return &abi_; }

    /// 把一次写回按 topic 前缀路由到注册的插件处理器。
    /// 返回 true 表示某插件已受理；无匹配前缀或插件拒绝返回 false。
    bool write(const TagValue &tag);

  private:
    struct WriteReg {
        std::string prefix;
        void *pluginCtx;
        IrPluginWriteFn handler;
    };

    // thunk 是经 C-ABI 函数指针被插件回调的入口。C++ 异常逃逸回插件即 UB，故全部 noexcept。
    static int pushTagThunk(void *ctx, const IrPluginTagValue *tag) noexcept;
    static int pushEventThunk(void *ctx, const IrPluginEvent *event) noexcept;
    static int pushStreamThunk(void *ctx, const IrPluginStreamFrame *frame) noexcept;
    static void registerWriterThunk(void *ctx, const char *prefix, void *pluginCtx,
                                    IrPluginWriteFn handler) noexcept;

    RuntimeApi *api_;
    IrPluginHostApi abi_{};
    std::vector<WriteReg> writers_;
};

} // namespace core
