// workloads.hpp - the two benchmark workloads, serial and parallel.
//
// Part of Parallel Numerics Bench. Written from scratch for this project.
//
// Both workloads share one determinism contract (details in docs/model.md):
//
//   1. The unit of work is a BLOCK of the iteration space, defined by
//      (first_index, last_index). The partitioning is a pure function of
//      (total_size, block_count), so serial and parallel runs that ask for
//      the same block_count produce the same blocks.
//   2. A single shared kernel computes one block. Serial code calls the
//      kernel once over the whole range; parallel code enqueues one pool
//      task per block calling the SAME kernel. Execution order between
//      blocks therefore cannot change any bit of any result.
//   3. Reductions are the only place order can matter, and each workload
//      states its own rule:
//        - Monte Carlo pi: hits are COUNTED IN INTEGERS. Integer addition
//          is exact and associative, so serial and parallel totals are
//          bit-identical for ANY partitioning.
//        - Matrix product: every output element is a dot product with a
//          fixed k-ascending accumulation order in the shared kernel, so
//          the matrix C is bit-identical serial vs parallel.
//        - Matrix checksum (a floating SUM over all elements): the parallel
//          path merges per-block partial sums in ascending block order.
//          Floating addition is not associative, so this checksum is NOT
//          bit-identical to the serial single-order sum in general - the
//          tests bound the difference with a relative tolerance instead,
//          and the merge order is fixed so the parallel checksum is still
//          bit-stable across runs.
//
// License: MIT (see LICENSE).

#ifndef PNB_WORKLOADS_HPP
#define PNB_WORKLOADS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "counter_rng.hpp"
#include "thread_pool.hpp"

namespace pnb {

// ---------------------------------------------------------------------------
// Deterministic partitioning: split [0, n) into `blocks` near-equal ranges.
// Pure function of (n, blocks); block b is [begin, end).
// ---------------------------------------------------------------------------
inline std::size_t block_begin(std::size_t n, std::size_t blocks, std::size_t b) {
    return (n * b) / blocks;
}
inline std::size_t block_end(std::size_t n, std::size_t blocks, std::size_t b) {
    return (n * (b + 1)) / blocks;
}

// ---------------------------------------------------------------------------
// Workload 1: Monte Carlo estimation of pi.
// Sample j (global index) uses stream j / B and counters 2*(j % B) and
// 2*(j % B) + 1 for its x and y coordinate, where B is the requested block
// size. The pair-to-(stream, counter) map is a pure function of j and B, so
// every sample value is reproducible independently of execution order.
// ---------------------------------------------------------------------------

// Shared kernel: count hits in [first, last) for the given block size B.
inline std::uint64_t mc_pi_block(std::size_t first, std::size_t last,
                                 std::size_t block_size, std::uint64_t stream_offset) {
    std::uint64_t hits = 0;
    for (std::size_t j = first; j < last; ++j) {
        const std::uint64_t stream = stream_offset + static_cast<std::uint64_t>(j / block_size);
        const std::uint64_t base = static_cast<std::uint64_t>(2 * (j % block_size));
        const double x = counter_unit(stream, base);
        const double y = counter_unit(stream, base + 1);
        if (x * x + y * y <= 1.0) {
            ++hits;
        }
    }
    return hits;
}

struct McPiResult {
    std::uint64_t hits = 0;   // exact hit count (integers: bit-identical)
    double estimate = 0.0;    // 4 * hits / n
    std::size_t blocks_used = 0;
};

inline McPiResult mc_pi_serial(std::size_t n, std::size_t block_size = 1) {
    if (n == 0) {
        throw std::invalid_argument("mc_pi: sample count must be >= 1");
    }
    if (block_size == 0) {
        throw std::invalid_argument("mc_pi: block size must be >= 1");
    }
    McPiResult r;
    r.hits = mc_pi_block(0, n, block_size, 0);
    r.estimate = 4.0 * static_cast<double>(r.hits) / static_cast<double>(n);
    r.blocks_used = 1;
    return r;
}

// Parallel: one task per block over the pool; integer partials summed in
// ascending block order (exact for integers, order cannot matter).
inline McPiResult mc_pi_parallel(ThreadPool& pool, std::size_t n,
                                 std::size_t block_count) {
    if (n == 0) {
        throw std::invalid_argument("mc_pi: sample count must be >= 1");
    }
    if (block_count == 0) {
        throw std::invalid_argument("mc_pi: block count must be >= 1");
    }
    const std::size_t block_size = (n + block_count - 1) / block_count;
    std::vector<std::future<std::uint64_t>> parts;
    parts.reserve(block_count);
    for (std::size_t b = 0; b < block_count; ++b) {
        const std::size_t first = block_begin(n, block_count, b);
        const std::size_t last = block_end(n, block_count, b);
        if (first == last) {
            continue;  // more blocks than samples: empty range contributes 0
        }
        parts.push_back(pool.enqueue(mc_pi_block, first, last, block_size,
                                     static_cast<std::uint64_t>(0)));
    }
    McPiResult r;
    r.blocks_used = parts.size();
    for (auto& f : parts) {
        r.hits += f.get();  // integer addition: exact, any order
    }
    r.estimate = 4.0 * static_cast<double>(r.hits) / static_cast<double>(n);
    return r;
}

// ---------------------------------------------------------------------------
// Workload 2: dense matrix product C = A * B (row-major double).
// ---------------------------------------------------------------------------

struct Matrix {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> data;

    Matrix() = default;
    Matrix(std::size_t r, std::size_t c, double fill = 0.0)
        : rows(r), cols(c), data(r * c, fill) {}

    double& operator()(std::size_t r, std::size_t c) {
        return data[r * cols + c];
    }
    const double& operator()(std::size_t r, std::size_t c) const {
        return data[r * cols + c];
    }
};

inline void validate_matmul(const Matrix& a, const Matrix& b) {
    if (a.rows == 0 || a.cols == 0 || b.rows == 0 || b.cols == 0) {
        throw std::invalid_argument("matmul: matrices must be non-empty");
    }
    if (a.cols != b.rows) {
        throw std::invalid_argument(
            "matmul: inner dimensions must match (A is " +
            std::to_string(a.rows) + "x" + std::to_string(a.cols) + ", B is " +
            std::to_string(b.rows) + "x" + std::to_string(b.cols) + ")");
    }
}

// Deterministic test input from the counter RNG (fixed stream keys so A and
// B are reproducible run to run and machine to machine).
inline Matrix random_matrix(std::size_t rows, std::size_t cols, std::uint64_t seed) {
    Matrix m(rows, cols);
    CounterRng rng(seed);
    for (std::size_t i = 0; i < rows; ++i) {
        for (std::size_t j = 0; j < cols; ++j) {
            m(i, j) = rng.next_unit();  // [0,1): dense, bounded, averaging ~0.5
        }
    }
    return m;
}

// Shared kernel: rows [r0, r1) of C = A * B. Every output element
// accumulates k = 0..K-1 ascending into a single accumulator - the exact
// same instruction sequence serial and parallel, hence bit-identical C.
inline void matmul_rows(const Matrix& a, const Matrix& b, Matrix& c,
                        std::size_t r0, std::size_t r1) {
    const std::size_t k_span = a.cols;
    const std::size_t n_cols = b.cols;
    for (std::size_t i = r0; i < r1; ++i) {
        for (std::size_t j = 0; j < n_cols; ++j) {
            double acc = 0.0;
            for (std::size_t k = 0; k < k_span; ++k) {
                acc += a(i, k) * b(k, j);
            }
            c(i, j) = acc;
        }
    }
}

struct MatmulResult {
    Matrix c;
    double checksum = 0.0;   // sum of all elements; see header comment for
                             // the reduction-order contract of this value
    std::size_t blocks_used = 0;
};

// Serial reference: one kernel call over all rows, plain row-major sum.
inline MatmulResult matmul_serial(const Matrix& a, const Matrix& b) {
    validate_matmul(a, b);
    MatmulResult r;
    r.c = Matrix(a.rows, b.cols);
    matmul_rows(a, b, r.c, 0, a.rows);
    double sum = 0.0;  // single sequential pass, row-major
    for (std::size_t i = 0; i < r.c.rows; ++i) {
        for (std::size_t j = 0; j < r.c.cols; ++j) {
            sum += r.c(i, j);
        }
    }
    r.checksum = sum;
    r.blocks_used = 1;
    return r;
}

// Parallel: one task per row-block (shared kernel), then checksum as
// per-block partial sums merged in ascending block order. Documented and
// tested as NOT bit-identical to the serial checksum (floating addition is
// not associative), only tolerance-equal; bit-stable across runs.
inline MatmulResult matmul_parallel(ThreadPool& pool, const Matrix& a, const Matrix& b,
                                    std::size_t block_count) {
    validate_matmul(a, b);
    if (block_count == 0) {
        throw std::invalid_argument("matmul: block count must be >= 1");
    }
    MatmulResult r;
    r.c = Matrix(a.rows, b.cols);
    std::vector<std::future<double>> partials;
    partials.reserve(block_count);
    for (std::size_t blk = 0; blk < block_count; ++blk) {
        const std::size_t r0 = block_begin(a.rows, block_count, blk);
        const std::size_t r1 = block_end(a.rows, block_count, blk);
        if (r0 == r1) {
            continue;
        }
        partials.push_back(pool.enqueue([&a, &b, &c = r.c, r0, r1]() -> double {
            matmul_rows(a, b, c, r0, r1);
            double block_sum = 0.0;  // row-major within the block
            for (std::size_t i = r0; i < r1; ++i) {
                for (std::size_t j = 0; j < c.cols; ++j) {
                    block_sum += c(i, j);
                }
            }
            return block_sum;
        }));
    }
    r.blocks_used = partials.size();
    double merged = 0.0;  // ascending block order, left-associated
    for (auto& f : partials) {
        merged += f.get();
    }
    r.checksum = merged;
    return r;
}

}  // namespace pnb

#endif  // PNB_WORKLOADS_HPP
