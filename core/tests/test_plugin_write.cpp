#include <variant>

#include "plugin_host/plugin_host.hpp"
#include "plugin_manager/plugin_manager.hpp"
#include "runtime_engine/runtime_engine.hpp"
#include "tests/test_util.hpp"

#ifndef EXAMPLE_PLUGIN_PATH
#define EXAMPLE_PLUGIN_PATH ""
#endif

// 验证写回下行链路：应用 -> RuntimeEngine.writeTag -> PluginHost(按前缀路由)
// -> 插件 onWrite -> 模拟设备回读 pushTag -> 落入 TagEngine。
int main() {
    using namespace core;

    RuntimeEngine runtime;
    runtime.start();

    PluginHost host(runtime);
    // 写回出口接到 PluginHost（按 topic 前缀路由到对应插件）。
    runtime.setWriteHandler([&host](const TagValue &t) { return host.write(t); });

    PluginManager manager(host.abi());
    IR_CHECK(manager.load(EXAMPLE_PLUGIN_PATH));
    IR_CHECK(manager.startAll());

    // 下发写：example/setpoint = 42.0（命中插件声明的 "example/" 前缀）。
    IR_CHECK(runtime.writeTag(TagValue{"example/setpoint", 42.0}));

    // 插件 onWrite 把值回推为同名 Tag（设备回读），应出现在 TagEngine。
    auto sp = runtime.tags().read("example/setpoint");
    IR_CHECK(sp.has_value());
    if (sp) {
        IR_CHECK(std::holds_alternative<double>(sp->value));
        IR_CHECK(std::get<double>(sp->value) == 42.0);
    }

    // 前缀不匹配 -> 无人受理。
    IR_CHECK(!runtime.writeTag(TagValue{"other/x", std::int32_t{1}}));

    manager.stopAll();
    runtime.stop();

    IR_TEST_REPORT();
}
