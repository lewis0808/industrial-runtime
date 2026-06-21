// 测试夹具插件：注册 "demo/" 前缀 ownership，SET 时原样 echo 回 TagEngine。
// 不连任何真实设备。供 sdk/irsp-client/JS/examples 写回测试用。
//
// 协议约定：
//   demo/__probe__   保留 topic，用于插件存在性检测（任意值都 echo）
//   demo/batch/<n>   用于批量写回
//   demo/<anything>  其它任意 topic 都 echo

#include <cstdint>
#include <new>
#include <string>
#include <string_view>

#include "irplugin/plugin.hpp"

namespace {

class DemoWritebackPlugin final : public irplugin::IPlugin {
  public:
    explicit DemoWritebackPlugin(const IrPluginHostApi *host) noexcept : host_(host) {}

    bool init() override {
        if (!host_.valid()) return false;
        // 声明：本插件负责 "demo/" 前缀下 Tag 的写回。
        host_.onWrite("demo/", [this](const IrPluginTagValue &t) { return onWrite(t); });
        return true;
    }

    bool start() override {
        host_.pushEvent("demo-writeback", "state", "demo-writeback started", IRPLUGIN_SEV_INFO);
        return true;
    }

    bool stop() override { return true; }

    bool destroy() override {
        delete this;
        return true;
    }

  private:
    bool onWrite(const IrPluginTagValue &t) {
        const std::string name(t.name.data, t.name.len);
        // 设备 echo：把值原样回推为 Tag（设备回读语义）
        switch (t.value.type) {
        case IRPLUGIN_TYPE_BOOL:
            host_.pushTag(name, t.value.as.boolean != 0);
            break;
        case IRPLUGIN_TYPE_INT32:
            host_.pushTag(name, static_cast<std::int32_t>(t.value.as.i32));
            break;
        case IRPLUGIN_TYPE_INT64:
            host_.pushTag(name, static_cast<std::int64_t>(t.value.as.i64));
            break;
        case IRPLUGIN_TYPE_FLOAT:
            host_.pushTag(name, t.value.as.f32);
            break;
        case IRPLUGIN_TYPE_DOUBLE:
            host_.pushTag(name, t.value.as.f64);
            break;
        case IRPLUGIN_TYPE_STRING:
            host_.pushTag(name, std::string_view(t.value.as.str.data, t.value.as.str.len));
            break;
        default:
            return false;
        }
        host_.pushEvent("demo-writeback", "writeback",
                        "echo " + name, IRPLUGIN_SEV_INFO);
        return true;
    }

    irplugin::Host host_;
};

} // namespace

IRPLUGIN_EXPORT IrPluginInfo getPluginInfo() {
    return IrPluginInfo{IRPLUGIN_ABI_VERSION, "demo-writeback",
                        "Demo Writeback Plugin (test fixture)", "1.0.0"};
}

IRPLUGIN_EXPORT int createPlugin(const IrPluginHostApi *host, const char * /*config_path*/,
                                 IrPluginInstance *out) {
    return irplugin::makeInstance(new (std::nothrow) DemoWritebackPlugin(host), out);
}
