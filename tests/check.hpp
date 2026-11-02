#pragma once
// Kit 自测用的最小断言宏，不引入外部测试框架。

#include <cmath>
#include <cstdio>

inline int g_check_failures = 0;

#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);             \
            ++g_check_failures;                                                          \
        }                                                                                \
    } while (false)

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        if (!((actual) == (expected))) {                                                 \
            std::printf("FAIL %s:%d  %s != %s\n", __FILE__, __LINE__, #actual,           \
                        #expected);                                                       \
            ++g_check_failures;                                                          \
        }                                                                                \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                          \
    do {                                                                                 \
        const double a = static_cast<double>(actual);                                    \
        const double b = static_cast<double>(expected);                                  \
        if (!(std::fabs(a - b) <= (tolerance))) {                                        \
            std::printf("FAIL %s:%d  %s=%f 期望 %f (容差 %f)\n", __FILE__, __LINE__,     \
                        #actual, a, b, static_cast<double>(tolerance));                   \
            ++g_check_failures;                                                          \
        }                                                                                \
    } while (false)

inline int CheckSummary(const char* suite) {
    if (g_check_failures != 0) {
        std::printf("[FAIL] %s：%d 个断言失败\n", suite, g_check_failures);
        return 1;
    }
    std::printf("[ OK ] %s\n", suite);
    return 0;
}
