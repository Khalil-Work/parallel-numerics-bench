// test_validation.cpp - input validation of pool and workloads.
//
// License: MIT (see LICENSE).

#include <stdexcept>

#include "test_util.hpp"
#include "thread_pool.hpp"
#include "workloads.hpp"

namespace {

template <class Fn>
bool throws_invalid_argument(Fn&& fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;  // wrong exception type
    }
    return false;  // no exception at all
}

}  // namespace

int main() {
    CHECK(throws_invalid_argument([] { pnb::ThreadPool pool(0); (void)pool; }));

    CHECK(throws_invalid_argument([] { (void)pnb::mc_pi_serial(0, 1); }));
    CHECK(throws_invalid_argument([] { (void)pnb::mc_pi_serial(10, 0); }));
    {
        pnb::ThreadPool pool(2);
        CHECK(throws_invalid_argument([&] { (void)pnb::mc_pi_parallel(pool, 0, 4); }));
        CHECK(throws_invalid_argument([&] { (void)pnb::mc_pi_parallel(pool, 10, 0); }));
    }

    CHECK(throws_invalid_argument([] {
        const pnb::Matrix empty(0, 3);
        const pnb::Matrix b(3, 3);
        (void)pnb::matmul_serial(empty, b);
    }));
    CHECK(throws_invalid_argument([] {
        const pnb::Matrix a(2, 3);   // inner dimensions do not match
        const pnb::Matrix b(2, 3);
        (void)pnb::matmul_serial(a, b);
    }));
    {
        pnb::ThreadPool pool(2);
        const pnb::Matrix a(4, 4);
        const pnb::Matrix b(4, 4);
        CHECK(throws_invalid_argument([&] { (void)pnb::matmul_parallel(pool, a, b, 0); }));
        CHECK(throws_invalid_argument([&] {
            const pnb::Matrix bad(4, 5);
            (void)pnb::matmul_parallel(pool, bad, b, 2);
        }));
    }

    // Valid edge inputs still work: single sample, single block, 1x1 matrix.
    {
        pnb::ThreadPool pool(1);
        const pnb::McPiResult one = pnb::mc_pi_parallel(pool, 1, 1);
        CHECK(one.estimate == 0.0 || one.estimate == 4.0);  // one sample

        pnb::Matrix a(1, 1), b(1, 1);
        a(0, 0) = 3.0;
        b(0, 0) = 5.0;
        const pnb::MatmulResult r = pnb::matmul_parallel(pool, a, b, 1);
        CHECK(r.c(0, 0) == 15.0);
    }

    return test_summary("test_validation");
}
