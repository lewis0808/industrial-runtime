#include <atomic>
#include <stdexcept>
#include <thread>

#include "common/tag_value.hpp"
#include "irplugin/plugin_abi.h"
#include "plugin_host/plugin_host.hpp"
#include "runtime_engine/runtime_api.hpp"
#include "tests/test_util.hpp"

namespace {

/// push* 一律抛异常，用于验证 thunk 把宿主侧异常拦在 C-ABI 边界内。
struct ThrowingApi : core::RuntimeApi {
    bool pushTag(const core::TagValue &) override { throw std::runtime_error("boom"); }
    bool pushEvent(const core::Event &) override { throw std::runtime_error("boom"); }
    bool pushStream(const core::StreamFrame &) override { throw std::runtime_error("boom"); }
};

/// push* 恒成功，用于写回路由的正常路径。
struct OkApi : core::RuntimeApi {
    bool pushTag(const core::TagValue &) override { return true; }
    bool pushEvent(const core::Event &) override { return true; }
    bool pushStream(const core::StreamFrame &) override { return true; }
};

} // namespace

int main() {
    using namespace core;

    // 写回处理器抛异常：宿主侧拦截，按未受理（false）处理，且不崩溃。
    {
        OkApi api;
        PluginHost host(api);
        const auto *abi = host.abi();
        abi->register_writer(abi->ctx, "dev/", nullptr,
                             [](void *, const IrPluginTagValue *) -> int {
                                 throw std::runtime_error("boom");
                             });
        IR_CHECK(host.write(TagValue{"dev/x", 1}) == false);
    }

    // 写回处理器正常返回：命中前缀，受理（true）。
    {
        OkApi api;
        PluginHost host(api);
        const auto *abi = host.abi();
        abi->register_writer(abi->ctx, "ok/", nullptr,
                             [](void *, const IrPluginTagValue *) -> int { return 1; });
        IR_CHECK(host.write(TagValue{"ok/y", 2}) == true);
        // 无匹配前缀：返回 false。
        IR_CHECK(host.write(TagValue{"zzz/none", 3}) == false);
    }

    // 最长前缀匹配：多个前缀命中时取最长者，与注册顺序无关。
    {
        OkApi api;
        PluginHost host(api);
        const auto *abi = host.abi();
        // 先注册短前缀，再注册长前缀（验证不依赖注册顺序）。
        int hitShort = 0;
        int hitLong = 0;
        abi->register_writer(abi->ctx, "a/", &hitShort, [](void *ctx, const IrPluginTagValue *) {
            ++*static_cast<int *>(ctx);
            return 1;
        });
        abi->register_writer(abi->ctx, "a/b/", &hitLong, [](void *ctx, const IrPluginTagValue *) {
            ++*static_cast<int *>(ctx);
            return 1;
        });
        // "a/b/x" 同时匹配 "a/" 与 "a/b/"，应路由到更长的 "a/b/"。
        IR_CHECK(host.write(TagValue{"a/b/x", 1}) == true);
        IR_CHECK_EQ(hitLong, 1);
        IR_CHECK_EQ(hitShort, 0);
        // "a/y" 只匹配 "a/"。
        IR_CHECK(host.write(TagValue{"a/y", 2}) == true);
        IR_CHECK_EQ(hitShort, 1);
        IR_CHECK_EQ(hitLong, 1);
    }

    // 同前缀冲突：相同 prefix 第二次注册被忽略，以先注册者为准。
    {
        OkApi api;
        PluginHost host(api);
        const auto *abi = host.abi();
        int hitFirst = 0;
        int hitSecond = 0;
        abi->register_writer(abi->ctx, "dup/", &hitFirst, [](void *ctx, const IrPluginTagValue *) {
            ++*static_cast<int *>(ctx);
            return 1;
        });
        abi->register_writer(abi->ctx, "dup/", &hitSecond, [](void *ctx, const IrPluginTagValue *) {
            ++*static_cast<int *>(ctx);
            return 1;
        });
        IR_CHECK(host.write(TagValue{"dup/x", 1}) == true);
        IR_CHECK_EQ(hitFirst, 1);
        IR_CHECK_EQ(hitSecond, 0);
    }

    // 写回归属：撤销某 owner 后 write() 不再路由到它（热卸载第一步——摘除写回）。
    {
        OkApi api;
        PluginHost host(api);
        const auto *abi = host.abi();
        const auto owner = host.createOwner();
        host.setActiveOwner(owner);
        int hits = 0;
        abi->register_writer(abi->ctx, "own/", &hits, [](void *ctx, const IrPluginTagValue *) {
            ++*static_cast<int *>(ctx);
            return 1;
        });
        host.setActiveOwner(0);
        IR_CHECK(host.write(TagValue{"own/x", 1}) == true);
        IR_CHECK_EQ(hits, 1);
        // 撤销归属 + 排空（无在途调用，立即返回）后不再路由。
        host.retireOwner(owner);
        host.waitQuiescent(owner);
        IR_CHECK(host.write(TagValue{"own/x", 1}) == false);
        IR_CHECK_EQ(hits, 1);
        host.removeOwner(owner);
    }

    // 引用计数排空：写回处理器执行期间 waitQuiescent 必须阻塞，直到在途调用结束才放行。
    // 这是热卸载安全的核心——卸载 destroy/卸库前必须排空，否则 use-after-free。
    {
        OkApi api;
        PluginHost host(api);
        const auto *abi = host.abi();
        const auto owner = host.createOwner();
        host.setActiveOwner(owner);

        struct Ctl {
            std::atomic<bool> inHandler{false};
            std::atomic<bool> release{false};
        } ctl;
        abi->register_writer(abi->ctx, "blk/", &ctl, [](void *c, const IrPluginTagValue *) {
            auto *p = static_cast<Ctl *>(c);
            p->inHandler.store(true);
            while (!p->release.load()) {
                std::this_thread::yield();
            }
            return 1;
        });
        host.setActiveOwner(0);

        std::thread writer([&] { host.write(TagValue{"blk/x", 1}); });
        while (!ctl.inHandler.load()) { // 等 handler 进入（此时 inflight=1）
            std::this_thread::yield();
        }

        host.retireOwner(owner); // 摘除路由，但在途调用未结束
        std::atomic<bool> quiesced{false};
        std::thread waiter([&] {
            host.waitQuiescent(owner);
            quiesced.store(true);
        });
        // handler 仍阻塞 → inflight>0 → waitQuiescent 不得返回。
        for (int i = 0; i < 100000 && !quiesced.load(); ++i) {
            std::this_thread::yield();
        }
        IR_CHECK(quiesced.load() == false);

        ctl.release.store(true); // 放行 handler，inflight 归零
        waiter.join();
        writer.join();
        IR_CHECK(quiesced.load() == true);
        host.removeOwner(owner);
    }

    // push thunk：宿主 RuntimeApi 抛异常不得逃逸回插件，thunk 返回 0。
    {
        ThrowingApi api;
        PluginHost host(api);
        const auto *abi = host.abi();

        IrPluginTagValue tag{};
        tag.name = {"dev/x", 5};
        tag.value.type = IRPLUGIN_TYPE_INT32;
        tag.value.as.i32 = 1;
        IR_CHECK(abi->push_tag(abi->ctx, &tag) == 0);

        IrPluginEvent ev{};
        ev.source = {"dev", 3};
        ev.category = {"state", 5};
        ev.message = {"msg", 3};
        ev.severity = IRPLUGIN_SEV_INFO;
        IR_CHECK(abi->push_event(abi->ctx, &ev) == 0);

        const uint8_t payload[] = {1, 2, 3};
        IrPluginStreamFrame frame{};
        frame.source = {"cam", 3};
        frame.type = IRPLUGIN_STREAM_BINARY;
        frame.payload = payload;
        frame.payload_len = sizeof(payload);
        IR_CHECK(abi->push_stream(abi->ctx, &frame) == 0);
    }

    IR_TEST_REPORT();
}
