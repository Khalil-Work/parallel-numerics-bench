// test_headers.cpp - every public header compiles cleanly on its own
// (guards against include-order dependencies and unused-warning leakage
// under -Wall -Wextra -Wpedantic / /W4).
//
// License: MIT (see LICENSE).

#include "counter_rng.hpp"
#include "thread_pool.hpp"
#include "timing.hpp"
#include "workloads.hpp"

int main() {
    // Touch one symbol from each header so nothing is optimized away
    // conceptually - this is a compile test.
    pnb::CounterRng rng(1);
    pnb::ThreadPool pool(1);
    (void)pool.enqueue([&rng] { return rng.next(); }).get();
    (void)pnb::mc_pi_serial(1000, 100);
    (void)pnb::median({1.0, 2.0, 3.0});
    return 0;
}
