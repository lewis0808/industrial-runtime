#include <string>
#include <variant>

#include "plugin_host/plugin_host.hpp"
#include "plugin_manager/plugin_manager.hpp"
#include "runtime_engine/runtime_engine.hpp"
#include "tests/test_util.hpp"

#ifndef EXAMPLE_PLUGIN_PATH
#define EXAMPLE_PLUGIN_PATH ""
#endif

int main() {
    using namespace core;

    RuntimeEngine runtime;
    runtime.start();

    PluginHost host(runtime);
    PluginManager manager(host.abi());

    // 加载示例插件 DLL。
    IR_CHECK(manager.load(EXAMPLE_PLUGIN_PATH));
    IR_CHECK_EQ(manager.count(), std::size_t{1});

    const IrPluginInfo info = manager.infoAt(0);
    IR_CHECK(std::string{info.id} == "example");

    // 启动插件，触发数据推送（Tag 写入为同步操作）。
    IR_CHECK(manager.startAll());

    // 验证数据确实经 RuntimeApi 进入了 TagEngine。
    auto temp = runtime.tags().read("example.temperature");
    IR_CHECK(temp.has_value());
    if (temp) {
        IR_CHECK(std::holds_alternative<double>(temp->value));
        IR_CHECK(std::get<double>(temp->value) == 25.5);
    }

    auto running = runtime.tags().read("example.running");
    IR_CHECK(running.has_value());
    if (running) {
        IR_CHECK(std::holds_alternative<bool>(running->value));
        IR_CHECK(std::get<bool>(running->value) == true);
    }

    IR_CHECK(manager.stopAll());
    runtime.stop();

    IR_TEST_REPORT();
}
