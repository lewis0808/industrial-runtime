#include <stdexcept>

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
