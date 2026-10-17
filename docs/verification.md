# Verification evidence and limits

Recorded 2026-10-18 on the machine described below. Every number in this
file is copied from an actual run of the tests or the bench driver; the raw
records live in `docs/results/` (`bench.csv`, `equivalence.csv`,
`progress.csv`, `environment.txt`, `run_summary.json`).

Machine: 12th Gen Intel Core i5-12500H (12 cores: 8P+4E, 16 logical
processors), Windows 11, GCC 16.2.0 (MinGW-W64), Release build.
CTest: **9/9 tests passed** (total 1.74 s).

## Per-test evidence

| Test | Verifies | Result |
|---|---|---|
| `test_headers` | each header compiles standalone under `-Wall -Wextra -Wpedantic` | passed |
| `test_pool` | 0-thread rejection; future values for 200 tasks; void tasks; exception propagation with exact message and pool survival; destructor drains 100 queued tasks; exact task count (1000); argument forwarding; `queued_tasks()==0` after draining | passed (all checks) |
| `test_counter_rng` | pure-function determinism; cursor replay from any counter; stream distinctness; chi-square equidistribution per stream and merged (64 bins, observed in [10, 140] band, dof 63); pairwise stream correlation < 5/sqrt(L); lag-1 autocorrelation of merged interleaved stream < 5/sqrt(N); overlapping-window z-ensemble mean/variance in band; negative control (raw counter) FAILS equidistribution as required | passed (all checks; deterministic, cannot flake) |
| `test_partition` | partitioning covers [0,n) with no gaps/overlaps, max block spread <= 1, plus hand-checked spot values (n=10, blocks=4) | passed (7 sizes x 9 block counts) |
| `test_equivalence_mc` | serial vs parallel hit counts EXACTLY equal (bit-identical, integers) for n in {100003, 1000003, 8000009} x threads {1,2,3,4,8,16}; estimate within 6 sigma of pi | passed (18/18 cells bit-identical) |
| `test_equivalence_matmul` | C bit-identical (byte compare) for sizes {17, 96, 257} x threads {1,2,3,5,8,16}; checksum relative delta < 1e-12; hand-computed 2x2 known answer; identity x B = B exactly | passed (18/18 cells bit-identical) |
| `test_determinism` | fresh pools reproduce MC hits bit-exactly; two pools reproduce C and checksum bit-exactly; same seed => same matrix bits; repeated serial runs stable | passed |
| `test_validation` | `std::invalid_argument` on: pool(0), mc n=0, mc block_size=0, parallel n=0, parallel blocks=0, empty matrix, dimension mismatch (parallel too); valid edge inputs (1 sample, 1x1 matrix) work | passed |
| `test_timing` | median (odd/even/unsorted/outlier-robust) and min unit expectations | passed |

Additional evidence from the bench driver (same session):

* `equivalence.csv` (24 cells): `output_bit_identical=1` in every cell
  (MC hits AND matrix C); `parallel_rerun_bit_stable=1` in every cell;
  matmul checksum relative deltas: 0.0 at threads=1, up to **7.29e-15** at
  threads >= 2 (the documented floating non-associativity; see
  `equivalence_deltas.png`).
* `bench.csv` (25 cells x 9 runs): every kept repeat of every cell carries
  an identical result string (`run_summary.json`:
  `bench_results_bit_stable_across_repeats: true`) - in-run determinism of
  the parallel paths under real scheduling.
* Warning-free build: GCC `-Wall -Wextra -Wpedantic -Werror` (clean) and
  MSVC `/W4` in CI.

## Measured scaling (medians, cold run excluded)

See `docs/results/speedup_table.md` for the full 25-cell table with both
median and min/min speedups, and `scaling.png` for the scatter of every
repeat. Headlines and honest non-headlines:

* Monte Carlo pi, n=5,000,000: **9.36x** at 16 threads (serial 93.8 ms ->
  10.0 ms).
* Matrix product, n=768: **6.35x** at 16 threads (serial 1.506 s ->
  0.237 s).
* Monte Carlo pi, n=100,000 at 1 worker: **0.79x** - the pool path is
  SLOWER than serial at this size (dispatch overhead dominates a ~1.7 ms
  workload). Reported as measured.
* Matrix product, n=96 at 1 worker: **0.85x** - same story. Reported as
  measured.
* Threads=1 columns at larger sizes show up to ~1.1x "speedup"; both paths
  run the identical kernel, so this is CPU frequency-state drift between
  interleaved cells on this laptop, not a parallel effect. Speedups should
  be read from threads >= 2. (Methodology note in `docs/model.md`.)

## Limits of these results

* **Shared memory only.** One process, one socket, one OS thread pool. No
  MPI, no GPUs, no distribution.
* **No NUMA claims.** Nothing is pinned; hybrid P/E cores on the recording
  machine are scheduled by the OS, not by this code.
* **Machine-specific timings.** Every timing number above is a property of
  the i5-12500H laptop recorded in `run_summary.json` under whatever
  background load existed on 2026-10-18. Reproduce with `pnb_bench` on your
  own machine; expect different absolute values.
* **RNG scope.** The generator is a statistical RNG tested for the
  properties used here (equidistribution, stream independence, window
  statistics, determinism). It is not cryptographic and has not been run
  through TestU01 BigCrush.
* **Equivalence scope.** Bit-identity claims hold for the same
  partitioning (and for MC, any partitioning). Changing the partitioning
  changes MC sample values (different stream map) while preserving all
  statistical properties - that is expected behavior, not a bug.
* **Checksum tolerance.** The parallel checksum is deterministic across
  reruns but only tolerance-close (<= ~1e-14 relative, observed) to the
  serial checksum at threads >= 2. It is never claimed bit-identical.
