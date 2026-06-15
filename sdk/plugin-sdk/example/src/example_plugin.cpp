// 示例插件：演示 irplugin SDK 的标准用法。
// - start() 推送若干 Tag/事件，验证上行链路；
// - 注册 onWrite("example/")，演示写回（应用 SET -> Runtime -> 插件 -> 设备）：
//   收到写后模拟"写入设备成功"，把值原样回推为 Tag（设备回读）。

#include <cstdint>
#include <new>
#include <string>
#include <string_view>

#include "irplugin/plugin.hpp"

namespace {

class ExamplePlugin final : public irplugin::IPlugin {
  public:
    explicit ExamplePlugin(const IrPluginHostApi *host) noexcept : host_(host) {}

    bool init() override {
        if (!host_.valid()) {
            return false;
        }
        // 声明：本插件负责 "example/" 前缀下 Tag 的写回。
        host_.onWrite("example/", [this](const IrPluginTagValue &t) { return onWrite(t); });
        return true;
    }

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
    bool onWrite(const IrPluginTagValue &t) {
        const std::string name(t.name.data, t.name.len);
        // 模拟设备写入成功后回读：把值原样回推为 Tag。
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
            return false; // 不支持的类型
        }
        host_.pushEvent("example", "write", "wrote " + name, IRPLUGIN_SEV_INFO);
        return true;
    }

    irplugin::Host host_;
};

} // namespace

IRPLUGIN_EXPORT IrPluginInfo getPluginInfo() {
    return IrPluginInfo{IRPLUGIN_ABI_VERSION, "example", "Example Plugin", "1.0.0"};
}

// 第二参数为该插件配置文件完整路径（runtime 透传，本示例无需配置，忽略）。
// 用 makeInstance 把 IPlugin 封装进 C vtable 填入 out：成功返回 1，失败返回 0。
// nothrow new 避免 bad_alloc 跨 DLL 边界逃逸（分配失败时 makeInstance 收到 null 返回 0）。
IRPLUGIN_EXPORT int createPlugin(const IrPluginHostApi *host, const char * /*config_path*/,
                                 IrPluginInstance *out) {
    return irplugin::makeInstance(new (std::nothrow) ExamplePlugin(host), out);
}
