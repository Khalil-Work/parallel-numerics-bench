// counter_rng.hpp - a counter-based pseudo-random generator (CBRNG).
//
// Part of Parallel Numerics Bench. Written from scratch for this project.
//
// Design (full discussion in docs/model.md):
//
//   sample(stream, counter) = mix64( mix64(stream + GOLDEN_GAMMA)
//                                    ^ mix64(counter + GOLDEN_GAMMA) )
//
//   * mix64 is the well-known finalizer cascade used by SplitMix64-style
//     generators (multiply-xorshift-multiply-xorshift-multiply-xorshift);
//     every stream and every counter passes through it, so neither raw
//     sequential counters nor raw stream ids ever reach the output.
//   * The generator is COUNTER-BASED: any individual sample is reproducible
//     from the pair (stream, counter) alone - no replay of prior state.
//     This is what makes deterministic parallel partitioning trivial:
//     global sample j in a block of size B maps to
//         stream = j / B,  counter = j % B
//     and serial and parallel code compute the exact same sample values
//     for the same B, in any execution order.
//   * STREAM SPLITTING: stream 0, 1, 2, ... are independent keys; samples
//     from different streams do not share state. Independence is tested in
//     tests/test_counter_rng.cpp (equidistribution, lag-1 autocorrelation
//     and an overlapping-window test on merged interleaved streams, plus a
//     deliberately broken "raw counter" negative control that must FAIL).
//
// Quality scope (honest): this is a 64-bit statistical RNG for Monte Carlo
// point generation, NOT a cryptographic generator, and it is not
// TestU01-BigCrush-certified. The shipped tests cover the properties the
// workloads rely on.
//
// License: MIT (see LICENSE).

#ifndef PNB_COUNTER_RNG_HPP
#define PNB_COUNTER_RNG_HPP

#include <cstdint>

namespace pnb {

namespace detail {

inline constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

// SplitMix64-style finalizer: avalanche every input bit over all 64 outputs.
inline std::uint64_t mix64(std::uint64_t z) noexcept {
    z += detail::kGoldenGamma;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace detail

// Stateless single-sample function: the whole generator in one address.
inline std::uint64_t counter_sample(std::uint64_t stream, std::uint64_t counter) noexcept {
    return detail::mix64(detail::mix64(stream) ^ detail::mix64(counter));
}

// Convenience double in [0, 1) with 53-bit resolution (standard top-53 map).
inline double counter_unit(std::uint64_t stream, std::uint64_t counter) noexcept {
    return static_cast<double>(counter_sample(stream, counter) >> 11) * 0x1.0p-53;
}

// Sequential cursor over one stream - handy for serial reference code.
class CounterRng {
public:
    explicit CounterRng(std::uint64_t stream, std::uint64_t start_counter = 0) noexcept
        : stream_(stream), counter_(start_counter) {}

    std::uint64_t next() noexcept { return counter_sample(stream_, counter_++); }
    double next_unit() noexcept { return counter_unit(stream_, counter_++); }

    std::uint64_t stream() const noexcept { return stream_; }
    std::uint64_t counter() const noexcept { return counter_; }

private:
    std::uint64_t stream_;
    std::uint64_t counter_;
};

// Negative-control generator for test-power validation: returns the raw
// counter with no mixing. Any sane statistical test must reject this.
inline std::uint64_t raw_counter_sample(std::uint64_t /*stream*/,
                                        std::uint64_t counter) noexcept {
    return counter;
}

}  // namespace pnb

#endif  // PNB_COUNTER_RNG_HPP
