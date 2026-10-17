// test_equivalence_matmul.cpp - serial vs parallel matrix product.
//
// Contract under test:
//   * C is bit-identical serial vs parallel for every size and thread
//     count: row-block partitioning, and both variants call the SAME kernel
//     with the same fixed k-ascending accumulation order per element.
//   * The floating-point checksum (sum over all elements) is merged from
//     per-block partials in ascending block order in the parallel path, so
//     it is NOT claimed bit-identical to the serial single-order sum; it is
//     checked against a relative tolerance instead (and bit-stable across
//     reruns in test_determinism).
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
    const std::size_t sizes[] = {17, 96, 257};
    const std::size_t thread_counts[] = {1, 2, 3, 5, 8, 16};

    for (std::size_t s : sizes) {
        const pnb::Matrix a = pnb::random_matrix(s, s, 0x4142);
        const pnb::Matrix b = pnb::random_matrix(s, s, 0x4344);
        const pnb::MatmulResult ser = pnb::matmul_serial(a, b);

        for (std::size_t t : thread_counts) {
            pnb::ThreadPool pool(t);
            const pnb::MatmulResult par = pnb::matmul_parallel(pool, a, b, t);

            // Bit-identical output matrix: vector<double>::operator== is an
            // exact element-wise comparison (no NaNs in these products).
            CHECK(par.c.data == ser.c.data);

            // Checksum: tolerance-level agreement only. The parallel merge
            // order (block partials, ascending) differs from the serial
            // single pass, and floating addition is not associative.
            const double denom =
                std::fabs(ser.checksum) > 0.0 ? std::fabs(ser.checksum) : 1.0;
            const double rel = std::fabs(par.checksum - ser.checksum) / denom;
            CHECK(rel < 1e-12);
        }
    }

    // Known-answer checks (values chosen to be exact in binary doubles).
    {
        pnb::Matrix a(2, 2);
        pnb::Matrix b(2, 2);
        a(0, 0) = 1.0; a(0, 1) = 2.0; a(1, 0) = 3.0; a(1, 1) = 4.0;
        b(0, 0) = 5.0; b(0, 1) = 6.0; b(1, 0) = 7.0; b(1, 1) = 8.0;
        pnb::ThreadPool pool(2);
        const pnb::MatmulResult r = pnb::matmul_parallel(pool, a, b, 2);
        CHECK(r.c(0, 0) == 19.0);  // 1*5 + 2*7
        CHECK(r.c(0, 1) == 22.0);  // 1*6 + 2*8
        CHECK(r.c(1, 0) == 43.0);  // 3*5 + 4*7
        CHECK(r.c(1, 1) == 50.0);  // 3*6 + 4*8
    }
    {
        // Identity: C = I * B must equal B exactly (0*x + 1*y = y).
        const std::size_t n = 9;
        pnb::Matrix ident(n, n);
        for (std::size_t i = 0; i < n; ++i) {
            ident(i, i) = 1.0;
        }
        const pnb::Matrix b = pnb::random_matrix(n, n, 0x77);
        pnb::ThreadPool pool(3);
        const pnb::MatmulResult r = pnb::matmul_parallel(pool, ident, b, 4);
        CHECK(r.c.data == b.data);
    }

    return test_summary("test_equivalence_matmul");
}
