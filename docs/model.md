# Model and methodology

This document states the mathematics of the two workloads, the design of the
counter-based RNG, the exact reduction-order semantics that make serial and
parallel results comparable, and the timing methodology with its statistical
honesty rules. Everything here corresponds to code in `src/` and is checked
by the tests in `tests/`.

## 1. Workloads

### 1.1 Monte Carlo estimation of pi

Draw N points (x_j, y_j) uniformly from [0,1)^2 and count the hits inside
the quarter unit circle:

```
hits = #{ j : x_j^2 + y_j^2 <= 1 }
pi_hat = 4 * hits / N
```

Each sample is a Bernoulli trial with p = pi/4, so the estimator has
standard deviation

```
sigma = 4 * sqrt(p (1 - p) / N)  ~=  1.642 / sqrt(N)
```

which is the yardstick for the statistical-sanity checks (6 sigma) and for
the convergence animation. The hit COUNT is an integer; the estimate is a
single division of that integer by N.

### 1.2 Dense matrix product

C = A * B with row-major double matrices, A (n x n), B (n x n). Every output
element is a dot product of one row of A and one column of B accumulated
over k = 0..n-1 in ascending order into a single `double` accumulator. There
is no blocking beyond the row-range partitioning, no SIMD intrinsics, no
`-ffast-math`; the point is a reproducible reference kernel, not peak GFLOPS.

A checksum (sum of all elements of C) is recorded for the CSVs; its
reduction-order semantics are stated in section 3.

## 2. The counter-based RNG

### 2.1 Construction

```
mix64(z):  z += 0x9E3779B97F4A7C15
           z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
           z = (z ^ (z >> 27)) * 0x94D049BB133111EB
           return z ^ (z >> 31)

sample(stream, counter) = mix64( mix64(stream) ^ mix64(counter) )
unit(stream, counter)   = (sample >> 11) * 2^-53      // in [0, 1)
```

`mix64` is the finalizer cascade used by SplitMix64-style generators: it is
a bijection on 64-bit words with full avalanche, so every input bit diffuses
over all 64 output bits. Both the stream key and the counter pass through
it, and the two mixed values are XORed before a final avalanche - neither
raw sequential counters nor raw stream ids ever reach the output.

### 2.2 Why counter-based

A stateful sequential generator (LCG, Mersenne Twister, even sequential
SplitMix64) couples sample values to execution order: to reproduce sample
j you must have advanced the state j times. A counter-based generator
addresses every sample directly: sample j is a pure function of
`(stream, counter)` and can be recomputed in isolation, in any order, on
any thread. That property is what makes the determinism contract of
section 3 trivial to enforce rather than a fragile hope.

### 2.3 Stream splitting for parallel partitions

With a partition of the sample range [0, N) into blocks of size B
(B = ceil(N / blocks)), global sample j maps to

```
stream  = j / B          (integer division - the block id)
counter = j mod B
```

Each parallel task owns one stream; streams never share state. Sample values
are therefore IDENTICAL for serial and parallel code computing with the same
B, independent of scheduling. Different B (different partitioning) produce
different, equally valid sample sets - equivalence claims in this project
are always stated "for the same partitioning".

For the x/y pair of one MC sample, counters 2i and 2i+1 are used.

### 2.4 Tested properties (and what is NOT claimed)

`tests/test_counter_rng.cpp` checks, with fixed deterministic data:

* pure-function determinism of `sample(stream, counter)` and cursor replay;
* stream distinctness and pairwise stream correlation ~ 0;
* chi-square equidistribution per stream and on the merged sequence
  (64 bins, dof 63, band = mean +/- 6 sd);
* lag-1 autocorrelation of a MERGED INTERLEAVED sequence (adjacent samples
  from different streams) ~ 0;
* an overlapping-window test on the merged sequence: window means of W=16
  standardized to z = (mean - 1/2) * sqrt(12 W); the z ensemble must have
  mean 0 (bound 6 * sqrt(W/N), accounting for the overlap correlation of
  adjacent windows) and variance ~ 1 (band 0.7..1.3);
* a NEGATIVE CONTROL: the raw un-mixed counter must FAIL the
  equidistribution test, proving the test has power.

NOT claimed: cryptographic security, TestU01 BigCrush certification, or
suitability beyond statistical Monte Carlo sampling. The shipped tests cover
the properties the workloads rely on, nothing more.

## 3. Reduction-order semantics (the exact equivalence claims)

The unit of work is a BLOCK: a contiguous index range [first, last). The
partitioning `block_begin/block_end(n, blocks, b) = [n*b/blocks,
n*(b+1)/blocks)` is a pure function of (n, blocks). A single shared kernel
computes one block; serial code calls it once over the whole range,
parallel code enqueues one pool task per block calling the SAME kernel.
Scheduling order between blocks therefore cannot change any bit of any
result. With that structure, each result class has its own honest claim:

1. **MC hit count - bit-identical for ANY partitioning.** Hits are counted
   in `uint64_t`. Integer addition is exact and associative; the parallel
   path sums integer partials in ascending block order, but any order would
   give the same bits. Tested by exact `==` across sizes and thread counts.

2. **Matrix C - bit-identical for the same partitioning (and in fact for
   any row-block partitioning).** Each output element is computed by the
   shared kernel with a fixed k-ascending accumulation order; row blocks
   write disjoint elements; there is no cross-element reduction. Serial and
   parallel outputs are byte-compared in the tests and in the equivalence
   sweep.

3. **Matrix checksum - tolerance-level only, deliberately.** The serial
   checksum is one row-major pass: `sum = ((...((c00 + c01) + c02) ...)`.
   The parallel checksum computes a partial sum per row block (row-major
   inside the block) and merges the block partials left-to-right in
   ascending block order. Floating-point addition is NOT associative, so
   these two orders generally round differently: we do NOT claim
   bit-identity here. Measured relative differences on this machine are
   0 at threads=1 (identical order) and up to ~7.3e-15 at threads >= 2
   (see `docs/results/equivalence.csv`). The parallel merge order is FIXED,
   so the parallel checksum is still bit-stable across reruns - deterministic
   without being bit-equal to serial. That distinction is the point of
   including this value.

## 4. Timing methodology

* **Repeats.** Every (workload, size, threads) cell runs `--repeats + 1`
  times (default 8 kept + 1 cold). The first run of a cell is a cold run
  (cache/allocator warm-up) and is discarded by default; it remains in
  `bench.csv` with `kept=0` so nothing is hidden. 7 kept repeats is the
  floor (`--repeats` below 7 is clamped up).

* **Statistics.** Median and min of the kept repeats. The median is robust
  to scheduler spikes; the min approximates the noise floor. The mean is
  deliberately not used as a headline number because on a shared laptop a
  single background interrupt pulls it visibly; the full scatter of every
  repeat is drawn in `scaling.png` instead.

* **Measurement order.** Serial and parallel cells are interleaved round
  by round: round r runs every cell once. Measuring all repeats of one cell
  back-to-back (the intuitive loop order) lets CPU frequency drift and
  cache state bias later cells; interleaving exposes all cells to the same
  thermal/frequency history on average.

* **Machine context.** Compiler, build type, hardware concurrency, clock
  type and CPU string are recorded in `docs/results/environment.txt` and
  `run_summary.json`. Timings are properties of that one machine.

* **Honesty rules.**
  - Never report a speedup the measurements do not show: sub-unity
    speedups (e.g. MC pi n=100k at 1 worker: 0.79x) are printed as-is.
  - The threads=1 column compares the serial reference with the same
    computation dispatched through the pool. On this power-limited laptop
    some threads=1 cells measure up to ~1.1x "faster" than serial; both
    paths execute the identical kernel, so this is CPU frequency-state
    drift between interleaved cells, not a parallel effect. Speedups are
    read from the threads >= 2 columns.
  - No NUMA pinning, no core affinity, no priority games; the pool is a
    plain shared-memory thread pool.
  - The parallel timings include pool dispatch overhead by construction;
    the serial baseline does not. Small sizes therefore understate
    parallelism - reported, not hidden.

## 5. The convergence animation

`pnb_bench --mode progress` runs the MC workload serially with 60
checkpoints of 1,000,000 samples each (60,000,000 samples total, one stream
per checkpoint block) and records `(checkpoint, cumulative samples,
cumulative hits, estimate, |estimate - pi|)` to `progress.csv`.
`scripts/make_gif.py` renders exactly one frame per recorded checkpoint -
no interpolation, no synthesized points - so the animation shows the
measured convergence at the measured sample counts.
