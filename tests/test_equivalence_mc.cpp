// test_equivalence_mc.cpp - serial vs parallel Monte Carlo pi.
//
// Contract under test: for the SAME partitioning (same block size B =
// ceil(n / blocks)), every sample j maps to the same (stream, counter) pair
// in serial and parallel code, and hit counts are integers, so serial and
// parallel hits - and therefore estimates - must be exactly equal
// (bit-identical), for every thread count and size tested.
//
// License: MIT (see LICENSE).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_util.hpp"
#include "thread_pool.hpp"
#include "workloads.hpp"

int main() {
    const double pi = 3.14159265358979323846;
    const std::size_t sizes[] = {100003, 1000003, 8000009};
    const std::size_t thread_counts[] = {1, 2, 3, 4, 8, 16};

    for (std::size_t n : sizes) {
        for (std::size_t t : thread_counts) {
            pnb::ThreadPool pool(t);
            const std::size_t block_size = (n + t - 1) / t;
            const pnb::McPiResult ser = pnb::mc_pi_serial(n, block_size);
            const pnb::McPiResult par = pnb::mc_pi_parallel(pool, n, t);

            // Integer counting: bit-identical, exact equality, no tolerance.
            CHECK(ser.hits == par.hits);
            CHECK(ser.estimate == par.estimate);  // same hits, same n

            // Statistical sanity: the estimate must be within 6 sigma of pi
            // (sigma = 4 * sqrt(p(1-p)/n), p = pi/4). This is a property of
            // the RNG quality, not of the parallelism.
            const double p = pi / 4.0;
            const double sigma = 4.0 * std::sqrt(p * (1.0 - p) /
                                                 static_cast<double>(n));
            CHECK(std::fabs(par.estimate - pi) < 6.0 * sigma);
        }
    }

    // Serial reference with block size 1 (stream per sample) is self-
    // consistent: recomputing gives the identical integer count.
    {
        const pnb::McPiResult a = pnb::mc_pi_serial(100003, 1);
        const pnb::McPiResult b = pnb::mc_pi_serial(100003, 1);
        CHECK(a.hits == b.hits);
    }

    return test_summary("test_equivalence_mc");
}
