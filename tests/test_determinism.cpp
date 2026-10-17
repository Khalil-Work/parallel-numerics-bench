// test_determinism.cpp - whole-workload determinism across repeated runs
// with fresh thread pools (in-process). The same partitioning must give the
// same bits every time, for both workloads.
//
// Cross-process determinism is verified by the bench driver's equivalence
// mode (parallel_rerun_bit_stable columns) and by the recorded CSVs, where
// every repeat of a cell must carry an identical result string.
//
// License: MIT (see LICENSE).

#include <cstddef>
#include <vector>

#include "test_util.hpp"
#include "thread_pool.hpp"
#include "workloads.hpp"

int main() {
    // Monte Carlo pi: two fresh pools, identical hits.
    {
        const std::size_t n = 1000003;
        pnb::ThreadPool pool_a(4);
        pnb::ThreadPool pool_b(7);  // different pool, same partitioning (n, t)
        const pnb::McPiResult a = pnb::mc_pi_parallel(pool_a, n, 4);
        const pnb::McPiResult b = pnb::mc_pi_parallel(pool_b, n, 4);
        CHECK(a.hits == b.hits);
        CHECK(a.estimate == b.estimate);
    }

    // Matmul: two fresh pools, bit-identical C and bit-identical checksum
    // (the parallel merge order is fixed, so reruns reproduce every bit).
    {
        const std::size_t s = 128;
        const pnb::Matrix a = pnb::random_matrix(s, s, 0x4142);
        const pnb::Matrix b = pnb::random_matrix(s, s, 0x4344);
        pnb::ThreadPool pool_a(3);
        pnb::ThreadPool pool_b(6);
        const pnb::MatmulResult r1 = pnb::matmul_parallel(pool_a, a, b, 5);
        const pnb::MatmulResult r2 = pnb::matmul_parallel(pool_b, a, b, 5);
        CHECK(r1.c.data == r2.c.data);
        CHECK(r1.checksum == r2.checksum);
    }

    // Input generation is deterministic: same seed, same matrix, bit for bit.
    {
        const pnb::Matrix a = pnb::random_matrix(64, 64, 0x4142);
        const pnb::Matrix b = pnb::random_matrix(64, 64, 0x4142);
        CHECK(a.data == b.data);
        const pnb::Matrix c = pnb::random_matrix(64, 64, 0x4344);
        CHECK(!(a.data == c.data));  // different seed, different matrix
    }

    // Repeated serial runs are bit-stable too.
    {
        const pnb::McPiResult a = pnb::mc_pi_serial(100003, 25001);
        const pnb::McPiResult b = pnb::mc_pi_serial(100003, 25001);
        CHECK(a.hits == b.hits);
    }

    return test_summary("test_determinism");
}
