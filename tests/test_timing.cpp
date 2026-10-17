// test_timing.cpp - unit tests for the repeat-statistics helpers used by
// the timing methodology (median / min).
//
// License: MIT (see LICENSE).

#include <vector>

#include "test_util.hpp"
#include "timing.hpp"

int main() {
    CHECK_NEAR(pnb::median({3.0}), 3.0, 0.0);
    CHECK_NEAR(pnb::median({1.0, 2.0, 3.0}), 2.0, 0.0);
    CHECK_NEAR(pnb::median({3.0, 1.0, 2.0}), 2.0, 0.0);
    CHECK_NEAR(pnb::median({4.0, 1.0, 3.0, 2.0}), 2.5, 0.0);
    CHECK_NEAR(pnb::median({5.0, -1.0, 9.0, 1.0, 3.0}), 3.0, 0.0);
    CHECK_NEAR(pnb::minimum_of({3.0, 1.0, 2.0}), 1.0, 0.0);
    CHECK_NEAR(pnb::minimum_of({7.0}), 7.0, 0.0);

    // Empty input is a documented "no data" case (returns 0).
    CHECK_NEAR(pnb::median({}), 0.0, 0.0);
    CHECK_NEAR(pnb::minimum_of({}), 0.0, 0.0);

    // Outliers do not move the median (robustness rationale in model.md).
    const std::vector<double> with_outlier{1.0, 1.1, 1.2, 1.05, 1.15, 1.02, 1000.0};
    CHECK_NEAR(pnb::median(with_outlier), 1.1, 0.0);

    return test_summary("test_timing");
}
