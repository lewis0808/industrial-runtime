#pragma once

// 零依赖的极简测试断言工具。
// 每个测试文件自带 main()，失败时返回非零退出码，由 CTest 判定通过/失败。

#include <cstdio>

namespace irtest {

inline int g_failures = 0;
inline int g_checks = 0;

}  // namespace irtest

#define IR_CHECK(cond)                                                       \
    do {                                                                     \
        ++::irtest::g_checks;                                                \
        if (!(cond)) {                                                       \
            ++::irtest::g_failures;                                          \
            std::fprintf(stderr, "FAIL: %s  @ %s:%d\n", #cond, __FILE__,     \
                         __LINE__);                                          \
        }                                                                    \
    } while (0)

#define IR_CHECK_EQ(a, b)                                                    \
    do {                                                                     \
        ++::irtest::g_checks;                                                \
        if (!((a) == (b))) {                                                 \
            ++::irtest::g_failures;                                          \
            std::fprintf(stderr, "FAIL: %s == %s  @ %s:%d\n", #a, #b,        \
                         __FILE__, __LINE__);                                \
        }                                                                    \
    } while (0)

#define IR_TEST_REPORT()                                                     \
    do {                                                                     \
        std::fprintf(stderr, "checks=%d failures=%d\n", ::irtest::g_checks,  \
                     ::irtest::g_failures);                                  \
        return ::irtest::g_failures == 0 ? 0 : 1;                            \
    } while (0)
