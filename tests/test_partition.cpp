// test_partition.cpp - invariants of the deterministic partitioning helper.
//
// License: MIT (see LICENSE).

#include <cstddef>
#include <vector>

#include "test_util.hpp"
#include "workloads.hpp"

int main() {
    const std::size_t cases[] = {1, 2, 3, 7, 96, 100003, 1000003};
    const std::size_t block_counts[] = {1, 2, 3, 4, 5, 8, 13, 16, 101};

    for (std::size_t n : cases) {
        for (std::size_t blocks : block_counts) {
            std::vector<std::size_t> sizes;
            std::size_t covered = 0;
            std::size_t prev_end = 0;
            for (std::size_t b = 0; b < blocks; ++b) {
                const std::size_t r0 = pnb::block_begin(n, blocks, b);
                const std::size_t r1 = pnb::block_end(n, blocks, b);
                CHECK(r0 <= r1);          // well-formed range
                CHECK(r1 <= n);           // inside the iteration space
                CHECK(r0 == prev_end);    // contiguous: no gaps, no overlap
                prev_end = r1;
                covered += r1 - r0;
                sizes.push_back(r1 - r0);
            }
            CHECK(prev_end == n);         // full coverage
            CHECK(covered == n);

            // Near-equal: max block size - min block size <= 1 (0 when n is
            // divisible; empty ranges only when blocks > n).
            std::size_t mx = 0;
            std::size_t mn = n + 1;
            for (std::size_t s : sizes) {
                if (s > mx) mx = s;
                if (s < mn) mn = s;
            }
            CHECK(mx - mn <= 1);
            if (blocks <= n) {
                CHECK(mn >= 1);  // no empty ranges when blocks <= n
            }
        }
    }

    // Spot values anyone can verify by hand: n = 10, blocks = 4.
    CHECK(pnb::block_begin(10, 4, 0) == 0);
    CHECK(pnb::block_end(10, 4, 0) == 2);
    CHECK(pnb::block_begin(10, 4, 1) == 2);
    CHECK(pnb::block_end(10, 4, 1) == 5);
    CHECK(pnb::block_end(10, 4, 2) == 7);
    CHECK(pnb::block_end(10, 4, 3) == 10);

    return test_summary("test_partition");
}
