// S7 设备插件：真实设备插件形态 —— jthread 周期采集 + 写回。
//
// 后端经 S7Backend 抽象，默认用 snap7 真实客户端（Snap7Backend，TCP 连 PLC）。
// Tag 映射内置一张小表（配置化加载是后续 roadmap 项）。S7 DB 为大端存储，本插件负责大端编解码。

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "irplugin/plugin.hpp"
#include "snap7_backend.hpp"

namespace {

enum class S7Type { Real, DInt };

struct TagDef {
    const char *topic;
    int db;
    int start;
    S7Type type;
    bool writable;
};

// 内置 tag 映射（模拟 DB1）。真实插件应由配置驱动。
constexpr TagDef TAGS[] = {
    {"s7/db1/temperature", 1, 0, S7Type::Real, false},
    {"s7/db1/counter", 1, 4, S7Type::DInt, false},
    {"s7/db1/setpoint", 1, 8, S7Type::Real, true},
};

// ---- S7 大端编解码 ----
float beToFloat(const std::vector<std::uint8_t> &b) {
    if (b.size() < 4) {
        return 0.0F;
    }
    const auto u = (static_cast<std::uint32_t>(b[0]) << 24) |
                   (static_cast<std::uint32_t>(b[1]) << 16) |
                   (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
    float f = 0;
    std::memcpy(&f, &u, 4);
    return f;
}
std::int32_t beToI32(const std::vector<std::uint8_t> &b) {
    if (b.size() < 4) {
        return 0;
    }
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(b[0]) << 24) |
                                     (static_cast<std::uint32_t>(b[1]) << 16) |
                                     (static_cast<std::uint32_t>(b[2]) << 8) |
                                     static_cast<std::uint32_t>(b[3]));
}
std::vector<std::uint8_t> u32ToBe(std::uint32_t u) {
    return {static_cast<std::uint8_t>(u >> 24), static_cast<std::uint8_t>(u >> 16),
            static_cast<std::uint8_t>(u >> 8), static_cast<std::uint8_t>(u)};
}
std::vector<std::uint8_t> floatToBe(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    return u32ToBe(u);
}

class S7Plugin final : public irplugin::IPlugin {
  public:
    explicit S7Plugin(const IrPluginHostApi *host) noexcept : host_(host) {}

    bool init() override {
        if (!host_.valid()) {
            return false;
        }
        host_.onWrite("s7/", [this](const IrPluginTagValue &t) { return onWrite(t); });
        return true;
    }

    bool start() override {
        host_.pushEvent("s7", "state", "S7 plugin started", IRPLUGIN_SEV_INFO);
        poll_ = std::jthread([this](std::stop_token st) { pollLoop(st); });
        return true;
    }

    bool stop() override {
        poll_.request_stop();
        host_.pushEvent("s7", "state", "S7 plugin stopped", IRPLUGIN_SEV_INFO);
        return true;
    }

    bool destroy() override {
        delete this;
        return true;
    }

  private:
    void pollLoop(std::stop_token st) {
        using namespace std::chrono_literals;
        while (!st.stop_requested()) {
            backend_.tick();
            for (const auto &def : TAGS) {
                const auto bytes = backend_.readDb(def.db, def.start, 4);
                if (def.type == S7Type::Real) {
                    host_.pushTag(def.topic, beToFloat(bytes));
                } else {
                    host_.pushTag(def.topic, beToI32(bytes));
                }
            }
            std::this_thread::sleep_for(50ms);
        }
    }

    bool onWrite(const IrPluginTagValue &t) {
        const std::string name(t.name.data, t.name.len);
        for (const auto &def : TAGS) {
            if (!def.writable || name != def.topic) {
                continue;
            }
            if (def.type == S7Type::Real) {
                float f = 0;
                switch (t.value.type) {
                case IRPLUGIN_TYPE_FLOAT:
                    f = t.value.as.f32;
                    break;
                case IRPLUGIN_TYPE_DOUBLE:
                    f = static_cast<float>(t.value.as.f64);
                    break;
                case IRPLUGIN_TYPE_INT32:
                    f = static_cast<float>(t.value.as.i32);
                    break;
                default:
                    return false;
                }
                return backend_.writeDb(def.db, def.start, floatToBe(f));
            }
            // DInt
            std::int32_t v = 0;
            switch (t.value.type) {
            case IRPLUGIN_TYPE_INT32:
                v = t.value.as.i32;
                break;
            case IRPLUGIN_TYPE_INT64:
                v = static_cast<std::int32_t>(t.value.as.i64);
                break;
            default:
                return false;
            }
            return backend_.writeDb(def.db, def.start, u32ToBe(static_cast<std::uint32_t>(v)));
        }
        return false;
    }

    irplugin::Host host_;
    // 真实 S7 客户端，连本机 PLC（标准端口 102，rack 0 / slot 1）。
    // 连接参数后续应配置化；离线可改用 s7::SimulatedS7Backend。
    s7::Snap7Backend backend_{"127.0.0.1", 0, 1};
    std::jthread poll_;
};

} // namespace

IRPLUGIN_EXPORT IrPluginInfo getPluginInfo() {
    return IrPluginInfo{IRPLUGIN_ABI_VERSION, "s7", "S7 Plugin (snap7)", "0.1.0"};
}

IRPLUGIN_EXPORT irplugin::IPlugin *createPlugin(const IrPluginHostApi *host) {
    return new S7Plugin(host);
}
