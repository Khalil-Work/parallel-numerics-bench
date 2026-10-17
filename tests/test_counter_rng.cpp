// test_counter_rng.cpp - determinism, stream independence and statistical
// quality of the counter-based RNG, including an overlapping-window test on
// merged interleaved streams and a negative control that must FAIL the
// equidistribution test (proving the test has power).
//
// Every threshold below is fixed and every sequence deterministic, so these
// tests cannot flake: they either always pass (healthy generator) or always
// fail (broken generator).
//
// License: MIT (see LICENSE).

#include <cmath>
#include <cstdint>
#include <vector>

#include "counter_rng.hpp"
#include "test_util.hpp"

namespace {

constexpr std::uint64_t kStreams = 8;
constexpr std::size_t kPerStream = 8192;
constexpr std::size_t kMergedLen = static_cast<std::size_t>(kStreams) * kPerStream;

// Merged interleaved sequence: sample i comes from stream i % kStreams at
// counter i / kStreams. Adjacent samples alternate between streams, so
// window and autocorrelation statistics cross stream boundaries.
std::vector<double> merged_interleaved() {
    std::vector<double> m(kMergedLen);
    for (std::size_t i = 0; i < kMergedLen; ++i) {
        m[i] = pnb::counter_unit(static_cast<std::uint64_t>(i % kStreams),
                                 static_cast<std::uint64_t>(i / kStreams));
    }
    return m;
}

// Chi-square statistic of unit doubles binned into k bins.
double chi_square(const std::vector<double>& xs, unsigned bins) {
    std::vector<long long> obs(bins, 0);
    for (double x : xs) {
        unsigned b = 0;
        if (x < 0.0 || x >= 1.0) {
            return 1e30;  // out of range: instant catastrophic failure
        }
        b = static_cast<unsigned>(x * static_cast<double>(bins));
        if (b >= bins) {
            b = bins - 1;
        }
        ++obs[b];
    }
    const double e = static_cast<double>(xs.size()) / static_cast<double>(bins);
    double chi2 = 0.0;
    for (unsigned b = 0; b < bins; ++b) {
        const double d = static_cast<double>(obs[b]) - e;
        chi2 += d * d / e;
    }
    return chi2;
}

// Pearson correlation of two equally long sequences.
double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = a.size();
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (std::size_t i = 0; i < n; ++i) {
        sa += a[i];
        sb += b[i];
        saa += a[i] * a[i];
        sbb += b[i] * b[i];
        sab += a[i] * b[i];
    }
    const double fn = static_cast<double>(n);
    const double cov = sab / fn - (sa / fn) * (sb / fn);
    const double va = saa / fn - (sa / fn) * (sa / fn);
    const double vb = sbb / fn - (sb / fn) * (sb / fn);
    return cov / std::sqrt(va * vb);
}

}  // namespace

int main() {
    // --- Determinism: pure function of (stream, counter). ---
    for (std::uint64_t s = 0; s < 16; ++s) {
        for (std::uint64_t c = 0; c < 64; ++c) {
            CHECK(pnb::counter_sample(s, c) == pnb::counter_sample(s, c));
        }
    }
    // Sequential cursor reproduces the direct sample function.
    {
        pnb::CounterRng rng(9);
        for (std::uint64_t i = 0; i < 100; ++i) {
            CHECK(rng.next() == pnb::counter_sample(9, i));
        }
    }
    // A cursor started at counter k reproduces the tail of one started at 0.
    {
        pnb::CounterRng a(5);
        pnb::CounterRng b(5, 1000);
        for (std::uint64_t i = 0; i < 1000; ++i) {
            (void)a.next();
        }
        for (std::uint64_t i = 0; i < 100; ++i) {
            CHECK(a.next() == b.next());
        }
    }

    // --- Stream distinctness: different streams give different outputs. ---
    CHECK(pnb::counter_sample(0, 0) != pnb::counter_sample(1, 0));
    CHECK(pnb::counter_sample(0, 0) != pnb::counter_sample(0, 1));

    // --- Equidistribution (chi-square) per stream and merged. ---
    {
        std::vector<double> s0(kPerStream);
        for (std::size_t i = 0; i < kPerStream; ++i) {
            s0[i] = pnb::counter_unit(0, static_cast<std::uint64_t>(i));
        }
        const double chi2 = chi_square(s0, 64);
        // dof = 63: mean 63, sd = sqrt(2*63) ~ 11.2. Band = mean +- 6 sd,
        // floored at 10 to also reject pathologically uniform output.
        CHECK(chi2 > 10.0);
        CHECK(chi2 < 140.0);

        const std::vector<double> m = merged_interleaved();
        const double chi2m = chi_square(m, 64);
        CHECK(chi2m > 10.0);
        CHECK(chi2m < 140.0);
    }

    // --- Pairwise stream correlation ~ 0 (independence of stream keys). ---
    {
        const double bound = 5.0 / std::sqrt(static_cast<double>(kPerStream));
        const std::uint64_t pairs[][2] = {{0, 1}, {0, 7}, {3, 5}, {2, 6}};
        for (const auto& pr : pairs) {
            std::vector<double> a(kPerStream), b(kPerStream);
            for (std::size_t i = 0; i < kPerStream; ++i) {
                a[i] = pnb::counter_unit(pr[0], static_cast<std::uint64_t>(i));
                b[i] = pnb::counter_unit(pr[1], static_cast<std::uint64_t>(i));
            }
            const double r = correlation(a, b);
            CHECK(std::fabs(r) < bound);
        }
    }

    // --- Lag-1 autocorrelation of the MERGED interleaved sequence ---
    // (adjacent samples come from different streams: cross-stream leakage
    // would show up here).
    {
        const std::vector<double> m = merged_interleaved();
        double sxy = 0, sxx = 0, mx = 0, my = 0;
        const std::size_t n = m.size() - 1;
        for (std::size_t i = 0; i < n; ++i) {
            sxy += m[i] * m[i + 1];
            sxx += m[i] * m[i];
            mx += m[i];
            my += m[i + 1];
        }
        const double fn = static_cast<double>(n);
        const double cov = sxy / fn - (mx / fn) * (my / fn);
        const double vx = sxx / fn - (mx / fn) * (mx / fn);
        const double r1 = cov / vx;
        CHECK(std::fabs(r1) < 5.0 / std::sqrt(fn));
    }

    // --- Overlapping-window test on the merged stream. ---
    // Slide a window of W samples over the merged sequence; each window
    // mean is standardized to z = (mean - 1/2) * sqrt(12 W), which has
    // mean 0 and variance 1 for iid U(0,1) samples. Check the z ensemble.
    {
        const std::vector<double> m = merged_interleaved();
        constexpr std::size_t W = 16;
        const std::size_t nwin = m.size() - W + 1;
        double sum_z = 0, sum_zz = 0;
        for (std::size_t start = 0; start < nwin; ++start) {
            double mean = 0;
            for (std::size_t k = 0; k < W; ++k) {
                mean += m[start + k];
            }
            mean /= static_cast<double>(W);
            const double z = (mean - 0.5) * std::sqrt(12.0 * static_cast<double>(W));
            sum_z += z;
            sum_zz += z * z;
        }
        const double fn = static_cast<double>(nwin);
        const double z_mean = sum_z / fn;
        const double z_var = sum_zz / fn;  // about the known mean 0
        // Adjacent overlapping windows share W-1 samples, so the z ensemble
        // is strongly correlated: the mean of the ensemble tracks the global
        // sample mean and has standard deviation sqrt(W / N), not
        // 1 / sqrt(nwin). The bound below is 6 x that (deterministic data,
        // so no flake risk).
        CHECK(std::fabs(z_mean) < 6.0 * std::sqrt(static_cast<double>(W) /
                                                  static_cast<double>(kMergedLen)));
        CHECK(z_var > 0.7);
        CHECK(z_var < 1.3);
    }

    // --- Negative control: the raw counter (no mixing) MUST fail the
    // equidistribution test. This validates the test's power; if this
    // check ever fails, the test lost its teeth, not the generator.
    {
        std::vector<double> bad(kMergedLen);
        for (std::size_t i = 0; i < kMergedLen; ++i) {
            bad[i] = static_cast<double>(
                         pnb::raw_counter_sample(0, static_cast<std::uint64_t>(i)) >> 11) *
                     0x1.0p-53;
        }
        const double chi2 = chi_square(bad, 64);
        CHECK(chi2 > 140.0);  // the un-mixed counter is catastrophically bad
    }

    return test_summary("test_counter_rng");
}
