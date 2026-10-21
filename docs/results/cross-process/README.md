# Cross-process determinism record (2026-10-18)

The in-process determinism tests (`tests/test_determinism.cpp`) prove that
reruns inside one process reproduce results bit-for-bit. This record adds
the stronger cross-process check on the same machine and binary:

```
build/pnb_bench.exe --mode equivalence --out docs/results/cross-process/run1
build/pnb_bench.exe --mode equivalence --out docs/results/cross-process/run2
```

Two separate operating-system processes, each running the full equivalence
sweep (MC pi n in {100003, 1000003, 8000009} and matmul n in {17, 96, 257},
threads {1, 2, 4, 8, 16}, including parallel reruns).

Result:

* `run1/equivalence.csv` and `run2/equivalence.csv` are byte-identical
  (SHA-256 of both: `083d480876e53c2402610f823d20ca4b63cb8482ef6d5bf03fef157ddd2251b2`).
* Both are also byte-identical to the original session's
  `docs/results/equivalence.csv` committed on the main branch.

Every cell in all three files reports `output_bit_identical=1` (exact MC
hit counts; exact matrix C) and `parallel_rerun_bit_stable=1`, with the
checksum relative deltas matching to the last digit.

Meaning: with the same compiler, binary and partitioning, the parallel
workloads are deterministic across processes, threads and runs - not just
statistically close, but bit-for-bit. Scope: this machine (see
`../environment.txt` in the original results directory), shared memory,
no pinned cores.
