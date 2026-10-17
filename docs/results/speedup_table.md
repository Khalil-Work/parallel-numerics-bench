# Measured speedups (this machine, this run)

Speedup = serial median / parallel median for the same workload and
size. Serial and parallel medians come from the same session, same
process, interleaved cells. Times are wall-clock per run, medians of
the kept repeats (cold run excluded). Values below 1.0 mean the
parallel version was SLOWER - those cells are reported as-is.

| Workload | Size | Threads | Serial median (s) | Parallel median (s) | Speedup (median) | Speedup (min/min) |
|---|---|---|---|---|---|---|
| Monte Carlo pi | 100000 | 1 | 0.0016631 | 0.0021053 | 0.79x | 0.79x |
| Monte Carlo pi | 100000 | 2 | 0.0016631 | 0.00120505 | 1.38x | 1.20x |
| Monte Carlo pi | 100000 | 4 | 0.0016631 | 0.0007905 | 2.10x | 2.31x |
| Monte Carlo pi | 100000 | 8 | 0.0016631 | 0.0005221 | 3.19x | 3.06x |
| Monte Carlo pi | 100000 | 16 | 0.0016631 | 0.0003277 | 5.08x | 4.29x |
| Monte Carlo pi | 5000000 | 1 | 0.0938292 | 0.0892427 | 1.05x | 1.13x |
| Monte Carlo pi | 5000000 | 2 | 0.0938292 | 0.0466336 | 2.01x | 2.34x |
| Monte Carlo pi | 5000000 | 4 | 0.0938292 | 0.026354 | 3.56x | 4.37x |
| Monte Carlo pi | 5000000 | 8 | 0.0938292 | 0.0137526 | 6.82x | 8.47x |
| Monte Carlo pi | 5000000 | 16 | 0.0938292 | 0.0100245 | 9.36x | 10.04x |
| Matrix product | 96 | 1 | 0.0011209 | 0.0013163 | 0.85x | 0.87x |
| Matrix product | 96 | 2 | 0.0011209 | 0.0008077 | 1.39x | 1.11x |
| Matrix product | 96 | 4 | 0.0011209 | 0.00067695 | 1.66x | 1.41x |
| Matrix product | 96 | 8 | 0.0011209 | 0.0003914 | 2.86x | 2.83x |
| Matrix product | 96 | 16 | 0.0011209 | 0.00035785 | 3.13x | 2.63x |
| Matrix product | 384 | 1 | 0.132487 | 0.118307 | 1.12x | 1.20x |
| Matrix product | 384 | 2 | 0.132487 | 0.0771262 | 1.72x | 2.71x |
| Matrix product | 384 | 4 | 0.132487 | 0.038857 | 3.41x | 4.54x |
| Matrix product | 384 | 8 | 0.132487 | 0.0287557 | 4.61x | 5.38x |
| Matrix product | 384 | 16 | 0.132487 | 0.0190331 | 6.96x | 8.69x |
| Matrix product | 768 | 1 | 1.50581 | 1.39259 | 1.08x | 1.15x |
| Matrix product | 768 | 2 | 1.50581 | 0.86633 | 1.74x | 2.13x |
| Matrix product | 768 | 4 | 1.50581 | 0.548376 | 2.75x | 3.08x |
| Matrix product | 768 | 8 | 1.50581 | 0.318395 | 4.73x | 4.88x |
| Matrix product | 768 | 16 | 1.50581 | 0.237186 | 6.35x | 7.13x |

Notes (read before quoting any number above):

- Small sizes are dominated by pool dispatch and task granularity;
  sub-unity speedups there are real measurements, not errors.
- The threads=1 column compares the serial reference with the same
  computation dispatched through the pool with a single worker.
  On this laptop some threads=1 cells measure FASTER than serial
  (up to ~1.1x), which cannot be a parallel effect - the two paths
  run the identical kernel. It reflects CPU frequency-state
  differences between interleaved cells. Read speedups from the
  threads>=2 columns.
- Serial and parallel cells were interleaved round by round so both
  variants see the same thermal/frequency history on average;
  back-to-back same-cell timing would overstate absolute times.
- This is one shared-memory machine (see run_summary.json for the
  hardware string); other machines will produce other numbers.
- No NUMA, core-pinning or priority tricks are used anywhere.
- The parallel path includes thread-pool overhead by construction;
  the serial baseline does not, so speedups at small sizes are
  understated relative to a hypothetical bare-threads version.
