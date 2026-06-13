// 示例插件：演示 plugin-sdk 的标准实现方式。
// start() 时通过宿主推送一个 double Tag、一个 bool Tag 与一条事件，
// 用于端到端验证 “插件 -> RuntimeApi -> core” 的数据链路。

#include "irplugin/plugin.hpp"

namespace {

class ExamplePlugin final : public irplugin::IPlugin {
public:
    explicit ExamplePlugin(const IrPluginHostApi* host) noexcept : host_(host) {}

    bool init() override { return host_.valid(); }

    bool start() override {
        host_.pushTag("example/temperature", 25.5);
        host_.pushTag("example/running", true);
        host_.pushEvent("example", "state", "plugin started", IRPLUGIN_SEV_INFO);
        return true;
    }

    bool stop() override {
        host_.pushEvent("example", "state", "plugin stopped", IRPLUGIN_SEV_INFO);
        return true;
    }

    bool destroy() override {
        delete this;
        return true;
    }

private:
    irplugin::Host host_;
};

}  // namespace

IRPLUGIN_EXPORT IrPluginInfo getPluginInfo() {
    return IrPluginInfo{IRPLUGIN_ABI_VERSION, "example", "Example Plugin", "1.0.0"};
}

IRPLUGIN_EXPORT irplugin::IPlugin* createPlugin(const IrPluginHostApi* host) {
    return new ExamplePlugin(host);
}
