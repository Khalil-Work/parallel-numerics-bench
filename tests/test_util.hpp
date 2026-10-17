// test_util.hpp - minimal single-header test harness for Parallel Numerics
// Bench tests. Each test executable is one translation unit that includes
// this header, runs checks and returns test_summary() from main().
//
// License: MIT (see LICENSE).

#ifndef PNB_TEST_UTIL_HPP
#define PNB_TEST_UTIL_HPP

#include <cmath>
#include <cstdio>
#include <string>

inline int pnb_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++pnb_failures;                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        const double pnb_a_ = static_cast<double>(a);                        \
        const double pnb_b_ = static_cast<double>(b);                        \
        if (!(std::fabs(pnb_a_ - pnb_b_) <= (tol))) {                        \
            ++pnb_failures;                                                  \
            std::printf("FAIL %s:%d: |%s - %s| = %.6g > %.6g\n", __FILE__,   \
                        __LINE__, #a, #b,                                    \
                        std::fabs(pnb_a_ - pnb_b_), static_cast<double>(tol)); \
        }                                                                    \
    } while (0)

inline int test_summary(const char* name) {
    if (pnb_failures == 0) {
        std::printf("%s: all checks passed\n", name);
        return 0;
    }
    std::printf("%s: %d check(s) FAILED\n", name, pnb_failures);
    return 1;
}

#endif  // PNB_TEST_UTIL_HPP
