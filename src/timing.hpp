// timing.hpp - timing helpers and repeat statistics.
//
// Part of Parallel Numerics Bench.
//
// Methodology (docs/model.md has the full statement):
//   * every (workload, size, threads) cell is measured `repeats` times
//     (>= 7 by default 9);
//   * the FIRST repeat of each cell is discarded as a cold run (cache /
//     allocator warm-up) unless --keep-cold is passed;
//   * we report the MEDIAN and the MIN of the kept repeats; the median is
//     robust to scheduler noise, the min approximates the noise-free floor;
//   * the mean is deliberately not used as a headline (it is pulled by
//     outliers on shared machines);
//   * every individual repeat is written to the CSV - nothing is hidden,
//     the plots show the full scatter.
//
// License: MIT (see LICENSE).

#ifndef PNB_TIMING_HPP
#define PNB_TIMING_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

namespace pnb {

class Stopwatch {
public:
    void start() { begin_ = std::chrono::steady_clock::now(); }
    double stop_sec() {
        const auto end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> d = end - begin_;
        return d.count();
    }

private:
    std::chrono::steady_clock::time_point begin_;
};

// Median of a non-empty sample (average of the two middle values for even
// counts). Returns 0 for an empty vector (callers treat empty as "no data").
inline double median(std::vector<double> xs) {
    if (xs.empty()) {
        return 0.0;
    }
    const std::size_t n = xs.size();
    std::nth_element(xs.begin(), xs.begin() + static_cast<std::ptrdiff_t>(n / 2),
                     xs.end());
    double m = xs[n / 2];
    if (n % 2 == 0) {
        std::nth_element(xs.begin(),
                         xs.begin() + static_cast<std::ptrdiff_t>(n / 2 - 1), xs.end());
        m = 0.5 * (m + xs[n / 2 - 1]);
    }
    return m;
}

inline double minimum_of(const std::vector<double>& xs) {
    if (xs.empty()) {
        return 0.0;
    }
    return *std::min_element(xs.begin(), xs.end());
}

}  // namespace pnb

#endif  // PNB_TIMING_HPP
