#include <string>

#include "config/config.hpp"
#include "tests/test_util.hpp"

int main() {
    using namespace core;
    Config cfg;

    const char *json = R"({
        "logger": { "level": "debug", "console": true },
        "runtime": { "scheduler": { "threads": 4 } }
    })";
    IR_CHECK(cfg.loadString(json));

    IR_CHECK_EQ(cfg.get<std::string>("logger.level", "info"), std::string{"debug"});
    IR_CHECK_EQ(cfg.get<bool>("logger.console", false), true);
    IR_CHECK_EQ(cfg.get<int>("runtime.scheduler.threads", 1), 4);

    // 缺失键返回默认值。
    IR_CHECK_EQ(cfg.get<int>("runtime.missing", 7), 7);
    IR_CHECK_EQ(cfg.get<std::string>("a.b.c", "def"), std::string{"def"});

    // has / tryGet。
    IR_CHECK(cfg.has("logger.level"));
    IR_CHECK(!cfg.has("logger.nope"));
    IR_CHECK(cfg.tryGet<int>("runtime.scheduler.threads").has_value());
    IR_CHECK(!cfg.tryGet<int>("logger.level").has_value()); // 类型不匹配

    // 非法 JSON 加载失败。
    IR_CHECK(!cfg.loadString("{ not valid"));

    IR_TEST_REPORT();
}
