#include <string>

#include "admin_command.hpp"
#include "common/event.hpp"
#include "common/stream.hpp"
#include "common/tag_value.hpp"
#include "plugin_host/plugin_host.hpp"
#include "plugin_manager/plugin_manager.hpp"
#include "runtime_engine/runtime_api.hpp"
#include "tests/test_util.hpp"

namespace {

/// 最小 RuntimeApi 桩：构造 PluginHost/PluginManager 用，命令测试不触发数据面。
struct OkApi : core::RuntimeApi {
    bool pushTag(const core::TagValue &) override { return true; }
    bool pushEvent(const core::Event &) override { return true; }
    bool pushStream(const core::StreamFrame &) override { return true; }
};

bool startsWith(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }

} // namespace

int main() {
    OkApi api;
    core::PluginHost host(api);
    core::PluginManager pm(host); // 空，无插件加载

    // PLUGIN LIST：空列表 -> "OK 0\n"。
    IR_CHECK(admin::handleAdminCommand(pm, "PLUGIN LIST") == "OK 0\n");
    // 命令大小写不敏感。
    IR_CHECK(admin::handleAdminCommand(pm, "plugin list") == "OK 0\n");
    // 尾随 \r（Windows 客户端）不影响分词。
    IR_CHECK(admin::handleAdminCommand(pm, "PLUGIN LIST\r") == "OK 0\n");

    // UNLOAD / RELOAD 不存在的 id -> ERR。
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, "PLUGIN UNLOAD nope"), "ERR"));
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, "PLUGIN RELOAD nope"), "ERR"));

    // 空命令 / 未知命令 / 缺参 / 未知子命令 -> ERR。
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, ""), "ERR"));
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, "FOO"), "ERR"));
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, "PLUGIN"), "ERR"));
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, "PLUGIN UNLOAD"), "ERR"));
    IR_CHECK(startsWith(admin::handleAdminCommand(pm, "PLUGIN BOGUS"), "ERR"));

    IR_TEST_REPORT();
}
