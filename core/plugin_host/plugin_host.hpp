#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
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
/// 路由到对应插件。每个被加载的插件分配一个**写回归属 owner**：注册的处理器归于
/// 其 owner，热卸载时按 owner 撤销并**排空在途写回调用**（引用计数），使 write()
/// 永不调用到已卸载的插件代码。
class PluginHost {
  public:
    /// 写回归属标识。0 为"无归属"（运行时自身或测试直接经 abi() 注册，永不撤销）。
    using OwnerId = std::uint64_t;

    explicit PluginHost(RuntimeApi &api);

    PluginHost(const PluginHost &) = delete;
    PluginHost &operator=(const PluginHost &) = delete;

    [[nodiscard]] const IrPluginHostApi *abi() const noexcept { return &abi_; }

    /// 把一次写回按 topic 前缀路由到注册的插件处理器。
    /// 多个前缀命中时取**最长前缀**的处理器（与注册顺序无关）。
    /// 返回 true 表示某插件已受理；无匹配前缀、归属正被卸载或插件拒绝返回 false。
    bool write(const TagValue &tag);

    // ---- 写回归属与热卸载支持（供 PluginManager 调用，管理操作由其串行化） ----

    /// 分配一个写回归属 id（>=1）。不改变当前 active owner。
    OwnerId createOwner();

    /// 设定后续 register_writer 归属的 owner（0 = 无归属）。PluginManager 在调用插件
    /// init/start 前后设置/清零，使插件在生命周期回调内注册的写回自动归于该插件。
    void setActiveOwner(OwnerId owner) noexcept;

    /// 撤销某 owner 的全部写回注册：此后 write() 不再路由到它，挡住新的在途调用。
    void retireOwner(OwnerId owner);

    /// 阻塞直到某 owner 已无在途写回调用（排空）。须在 retireOwner 之后调用。
    void waitQuiescent(OwnerId owner) const;

    /// 删除某 owner 的归属状态（须在 retireOwner + waitQuiescent 之后）。
    void removeOwner(OwnerId owner) noexcept;

  private:
    /// 单个 owner 的在途写回计数。以 shared_ptr 持有，使 write() 在出锁调用插件期间，
    /// 即便该 owner 被 removeOwner 也不悬空（计数活到调用结束）。
    struct OwnerState {
        std::atomic<int> inflight{0};
    };

    struct WriteReg {
        std::string prefix;
        void *pluginCtx;
        IrPluginWriteFn handler;
        std::shared_ptr<OwnerState> owner; ///< 非空；归属的在途计数
    };

    // thunk 是经 C-ABI 函数指针被插件回调的入口。C++ 异常逃逸回插件即 UB，故全部 noexcept。
    static int pushTagThunk(void *ctx, const IrPluginTagValue *tag) noexcept;
    static int pushEventThunk(void *ctx, const IrPluginEvent *event) noexcept;
    static int pushStreamThunk(void *ctx, const IrPluginStreamFrame *frame) noexcept;
    static void registerWriterThunk(void *ctx, const char *prefix, void *pluginCtx,
                                    IrPluginWriteFn handler) noexcept;

    /// 取 owner 的计数状态（无锁版，调用方须持 writersMutex_）。owner 不存在返回 nullptr。
    std::shared_ptr<OwnerState> ownerStateLocked(OwnerId owner) const;

    RuntimeApi *api_;
    IrPluginHostApi abi_{};
    // 注册（register_writer）与撤销取独占锁，写回（write，IRSP 线程）取共享锁。
    // 支持运行期动态注册、并发写回与热卸载。
    mutable std::shared_mutex writersMutex_;
    std::vector<WriteReg> writers_;
    std::unordered_map<OwnerId, std::shared_ptr<OwnerState>> owners_; ///< 含 0（无归属）
    OwnerId activeOwner_{0};
    OwnerId nextOwner_{1};
};

} // namespace core
