// S7 插件真 snap7 端到端：测试内用 snap7 Srv_* 起一个进程内虚拟 PLC（注册 DB1 区），
// 插件经 Snap7Backend 真 TCP（S7comm）连上去读写。无需真实硬件。
//
// 链路：虚拟 PLC(DB1) <--真 S7comm--> S7 插件采集循环 --> pushTag --> TagEngine
//       应用 SET --> Runtime --> 插件 onWrite --> Cli_DBWrite --> 虚拟 PLC 的 DB1

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <variant>

#include <snap7/snap7_libmain.h>

#include "plugin_host/plugin_host.hpp"
#include "plugin_manager/plugin_manager.hpp"
#include "runtime_engine/runtime_engine.hpp"
#include "tests/test_util.hpp"

#ifndef S7_PLUGIN_PATH
#define S7_PLUGIN_PATH ""
#endif

namespace {

void putU32BE(unsigned char *b, int off, std::uint32_t u) {
    b[off] = static_cast<unsigned char>(u >> 24);
    b[off + 1] = static_cast<unsigned char>(u >> 16);
    b[off + 2] = static_cast<unsigned char>(u >> 8);
    b[off + 3] = static_cast<unsigned char>(u);
}
void putRealBE(unsigned char *b, int off, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    putU32BE(b, off, u);
}
void putI32BE(unsigned char *b, int off, std::int32_t v) {
    putU32BE(b, off, static_cast<std::uint32_t>(v));
}
float getRealBE(const unsigned char *b, int off) {
    const auto u = (static_cast<std::uint32_t>(b[off]) << 24) |
                   (static_cast<std::uint32_t>(b[off + 1]) << 16) |
                   (static_cast<std::uint32_t>(b[off + 2]) << 8) |
                   static_cast<std::uint32_t>(b[off + 3]);
    float f = 0;
    std::memcpy(&f, &u, 4);
    return f;
}

} // namespace

int main() {
    using namespace core;

    // ---- 虚拟 PLC（snap7 服务端），注册 DB1 区并填初值（大端）。----
    unsigned char db1[64] = {0};
    putRealBE(db1, 0, 25.5F); // DB1.DBD0 温度
    putI32BE(db1, 4, 7);      // DB1.DBD4 计数器

    S7Object server = Srv_Create();
    IR_CHECK(server != 0);
    Srv_RegisterArea(server, srvAreaDB, 1, db1, static_cast<int>(sizeof(db1)));
    const int rc = Srv_StartTo(server, "127.0.0.1");
    IR_CHECK(rc == 0);

    // ---- Runtime + 写回 + 加载 S7 插件 ----
    RuntimeEngine runtime;
    runtime.start();
    PluginHost host(runtime);
    runtime.setWriteHandler([&host](const TagValue &t) { return host.write(t); });
    PluginManager manager(host.abi());
    IR_CHECK(manager.load(S7_PLUGIN_PATH));
    IR_CHECK(manager.startAll());

    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 连接 + 多轮采集

    // 插件经真 S7comm 读到的值。
    auto temp = runtime.tags().read("s7/db1/temperature");
    IR_CHECK(temp.has_value());
    if (temp) {
        IR_CHECK(std::holds_alternative<float>(temp->value));
        IR_CHECK(std::get<float>(temp->value) == 25.5F);
    }
    auto counter = runtime.tags().read("s7/db1/counter");
    IR_CHECK(counter.has_value());
    if (counter) {
        IR_CHECK(std::holds_alternative<std::int32_t>(counter->value));
        IR_CHECK(std::get<std::int32_t>(counter->value) == 7);
    }

    // ---- 写回：SET 设定点 -> 真 Cli_DBWrite 写到虚拟 PLC ----
    IR_CHECK(runtime.writeTag(TagValue{"s7/db1/setpoint", 42.0F}));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    IR_CHECK(getRealBE(db1, 8) == 42.0F); // 服务端 DB1 被真写入
    auto sp = runtime.tags().read("s7/db1/setpoint");
    IR_CHECK(sp.has_value());
    if (sp) {
        IR_CHECK(std::holds_alternative<float>(sp->value));
        IR_CHECK(std::get<float>(sp->value) == 42.0F);
    }

    manager.stopAll();
    runtime.stop();
    Srv_Stop(server);
    Srv_Destroy(server); // 形参为 S7Object&

    IR_TEST_REPORT();
}
