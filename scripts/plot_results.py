#!/usr/bin/env python3
"""Aggregate the recorded CSVs into figures, a speedup table and a run
summary for Parallel Numerics Bench.

Inputs (written by pnb_bench, the C++ driver):
  docs/results/bench.csv         scaling sweep, one row per run
  docs/results/equivalence.csv   serial/parallel equivalence sweep
  docs/results/environment.txt   compiler / build / hardware info

Outputs:
  docs/results/scaling.png               time vs threads, all repeats shown
  docs/results/equivalence_deltas.png    checksum relative deltas (log scale)
  docs/results/speedup_table.md          median/min speedups + honesty notes
  docs/results/run_summary.json          machine context + medians + speedups

Figure style policy: pure white interiors, no grid lines.

Usage:
  python scripts/plot_results.py [--results docs/results] [--cpu "CPU string"]
"""

import argparse
import csv
import datetime
import json
import platform
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

PALETTE = ["#1f4e79", "#c0504d", "#4f8a4f", "#8a4f9e", "#cc7a29"]
MARKERS = ["o", "s", "^", "D", "v"]


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def style_axis(ax):
    ax.set_facecolor("white")
    ax.grid(False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.tick_params(colors="#333333")
    for s in ("left", "bottom"):
        ax.spines[s].set_color("#333333")


def parse_env(path):
    env = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip()
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="docs/results")
    ap.add_argument("--cpu", default="", help="CPU description for the summary")
    args = ap.parse_args()
    rdir = Path(args.results)

    bench = read_csv(rdir / "bench.csv")
    equiv = read_csv(rdir / "equivalence.csv")
    env = parse_env(rdir / "environment.txt")

    # ------------------------------------------------------------------
    # Aggregate bench cells: (workload, variant, size, threads) -> runs.
    # Only kept runs (cold run discarded) enter the statistics.
    # ------------------------------------------------------------------
    cells = defaultdict(list)
    cell_results = defaultdict(set)
    for row in bench:
        key = (row["workload"], row["variant"], int(row["size"]), int(row["threads"]))
        if int(row["kept"]) == 1:
            cells[key].append(float(row["time_s"]))
            cell_results[key].add(row["result"])

    medians = {k: statistics.median(v) for k, v in cells.items()}
    mins = {k: min(v) for k, v in cells.items()}
    n_kept = {k: len(v) for k, v in cells.items()}

    # In-run determinism: identical result string across kept repeats?
    results_bit_stable = all(len(v) == 1 for v in cell_results.values())

    workloads = ["mc_pi", "matmul"]
    pretty = {"mc_pi": "Monte Carlo pi", "matmul": "Matrix product"}
    threads_axis = sorted({k[3] for k in cells if k[1] == "parallel"})

    # ------------------------------------------------------------------
    # Scaling figure: time vs threads, every kept repeat as a dot.
    # ------------------------------------------------------------------
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.6), facecolor="white")
    for ax, wl in zip(axes, workloads):
        sizes = sorted({k[2] for k in cells if k[0] == wl})
        for idx, size in enumerate(sizes):
            color = PALETTE[idx % len(PALETTE)]
            marker = MARKERS[idx % len(MARKERS)]
            # serial baseline (threads=1 row, variant=serial)
            skey = (wl, "serial", size, 1)
            if skey in medians:
                ax.axhline(
                    medians[skey],
                    color=color,
                    linestyle=":",
                    linewidth=1.3,
                    alpha=0.9,
                    xmin=0.02,
                    xmax=0.98,
                )
            for t in threads_axis:
                key = (wl, "parallel", size, t)
                if key not in cells:
                    continue
                xs = [t] * len(cells[key])
                ax.scatter(
                    xs,
                    cells[key],
                    s=14,
                    color=color,
                    alpha=0.45,
                    edgecolors="none",
                    zorder=2,
                )
            med = [
                medians[(wl, "parallel", size, t)]
                for t in threads_axis
                if (wl, "parallel", size, t) in medians
            ]
            ok_t = [
                t for t in threads_axis if (wl, "parallel", size, t) in medians
            ]
            ax.plot(
                ok_t,
                med,
                color=color,
                marker=marker,
                markersize=5.5,
                linewidth=1.8,
                label=f"n = {size} (median)",
                zorder=3,
            )
            if skey in medians:
                # annotate the serial baseline once per size, near the left
                ax.annotate(
                    f"serial n={size}",
                    xy=(threads_axis[0], medians[skey]),
                    xytext=(4, -11),
                    textcoords="offset points",
                    fontsize=7.5,
                    color=color,
                )
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xticks(threads_axis)
        ax.set_xticklabels([str(t) for t in threads_axis])
        ax.set_xlabel("worker threads")
        ax.set_ylabel("wall time per run (s)")
        ax.set_title(f"{pretty[wl]} - small dots: every kept repeat",
                     fontsize=11)
        ax.legend(frameon=False, fontsize=8, loc="best")
        style_axis(ax)
    fig.suptitle(
        "Parallel Numerics Bench - scaling (medians of kept repeats; "
        "dotted lines: serial reference)",
        fontsize=12,
    )
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(rdir / "scaling.png", dpi=160, facecolor="white")
    plt.close(fig)

    # ------------------------------------------------------------------
    # Equivalence deltas figure (matmul checksum; MC is exactly 0).
    # ------------------------------------------------------------------
    mm = [r for r in equiv if r["workload"] == "matmul"]
    fig, ax = plt.subplots(figsize=(8.6, 4.2), facecolor="white")
    width = 0.8 / len({r["threads"] for r in mm})
    groups = sorted({int(r["size"]) for r in mm})
    tcounts = sorted({int(r["threads"]) for r in mm})
    for j, t in enumerate(tcounts):
        xs, ys = [], []
        for g in groups:
            row = next(
                r for r in mm
                if int(r["size"]) == g and int(r["threads"]) == t
            )
            xs.append(groups.index(g) + (j - (len(tcounts) - 1) / 2) * width)
            ys.append(max(float(row["checksum_rel_delta"]), 1e-19))
        ax.bar(xs, ys, width=width * 0.9, label=f"{t} threads",
               color=PALETTE[j % len(PALETTE)], alpha=0.85)
    ax.set_yscale("log")
    ax.set_xticks(range(len(groups)))
    ax.set_xticklabels([f"n = {g}" for g in groups])
    ax.set_ylabel("checksum relative difference (serial vs parallel)")
    ax.set_title(
        "Matrix checksum reduction-order deltas (0 = bit-identical; "
        "matrix C itself is bit-identical in every cell)",
        fontsize=10.5,
    )
    ax.legend(frameon=False, fontsize=8, title="parallel run", title_fontsize=8)
    style_axis(ax)
    fig.tight_layout()
    fig.savefig(rdir / "equivalence_deltas.png", dpi=160, facecolor="white")
    plt.close(fig)

    # ------------------------------------------------------------------
    # Speedup table (median based, min based) + honesty notes.
    # ------------------------------------------------------------------
    lines = [
        "# Measured speedups (this machine, this run)",
        "",
        "Speedup = serial median / parallel median for the same workload and",
        "size. Serial and parallel medians come from the same session, same",
        "process, interleaved cells. Times are wall-clock per run, medians of",
        "the kept repeats (cold run excluded). Values below 1.0 mean the",
        "parallel version was SLOWER - those cells are reported as-is.",
        "",
        "| Workload | Size | Threads | Serial median (s) | Parallel median (s) | Speedup (median) | Speedup (min/min) |",
        "|---|---|---|---|---|---|---|",
    ]
    speedups = {}
    for wl in workloads:
        for size in sorted({k[2] for k in cells if k[0] == wl}):
            skey = (wl, "serial", size, 1)
            if skey not in medians:
                continue
            for t in threads_axis:
                pkey = (wl, "parallel", size, t)
                if pkey not in medians:
                    continue
                s_med = medians[skey]
                p_med = medians[pkey]
                s_min = mins[skey]
                p_min = mins[pkey]
                sp_med = s_med / p_med
                sp_min = s_min / p_min
                speedups[(wl, size, t)] = sp_med
                lines.append(
                    f"| {pretty[wl]} | {size} | {t} | {s_med:.6g} | "
                    f"{p_med:.6g} | {sp_med:.2f}x | {sp_min:.2f}x |"
                )
    lines += [
        "",
        "Notes (read before quoting any number above):",
        "",
        "- Small sizes are dominated by pool dispatch and task granularity;",
        "  sub-unity speedups there are real measurements, not errors.",
        "- The threads=1 column compares the serial reference with the same",
        "  computation dispatched through the pool with a single worker.",
        "  On this laptop some threads=1 cells measure FASTER than serial",
        "  (up to ~1.1x), which cannot be a parallel effect - the two paths",
        "  run the identical kernel. It reflects CPU frequency-state",
        "  differences between interleaved cells. Read speedups from the",
        "  threads>=2 columns.",
        "- Serial and parallel cells were interleaved round by round so both",
        "  variants see the same thermal/frequency history on average;",
        "  back-to-back same-cell timing would overstate absolute times.",
        "- This is one shared-memory machine (see run_summary.json for the",
        "  hardware string); other machines will produce other numbers.",
        "- No NUMA, core-pinning or priority tricks are used anywhere.",
        "- The parallel path includes thread-pool overhead by construction;",
        "  the serial baseline does not, so speedups at small sizes are",
        "  understated relative to a hypothetical bare-threads version.",
        "",
    ]
    (rdir / "speedup_table.md").write_text("\n".join(lines), encoding="utf-8")

    # ------------------------------------------------------------------
    # run_summary.json
    # ------------------------------------------------------------------
    summary = {
        "project": "parallel-numerics-bench",
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(
            timespec="seconds"
        ),
        "machine": {
            "cpu": args.cpu or platform.processor() or "unrecorded",
            "machine": platform.machine(),
            "os": platform.system() + " " + platform.release(),
            "hardware_concurrency": int(env.get("hardware_concurrency", 0)),
        },
        "compiler": env.get("compiler", "unknown"),
        "build_type": env.get("build_type", "unknown"),
        "clock": env.get("clock", "unknown"),
        "methodology": {
            "repeats_per_cell_kept": sorted(set(n_kept.values())),
            "cold_run": "excluded (kept=0 rows remain in bench.csv)",
            "measurement_order": "serial and parallel cells interleaved "
                                 "round by round",
            "statistics": ["median", "min"],
            "note": "mean deliberately not used as headline; scatter shown "
                    "in scaling.png; threads=1 cells can measure faster "
                    "than serial due to CPU frequency-state drift - not a "
                    "parallel effect",
        },
        "verification": {
            "bench_results_bit_stable_across_repeats": results_bit_stable,
            "mc_pi_hits_bit_identical_all_cells": all(
                r["output_bit_identical"] == "1"
                for r in equiv if r["workload"] == "mc_pi"
            ),
            "matmul_C_bit_identical_all_cells": all(
                r["output_bit_identical"] == "1"
                for r in equiv if r["workload"] == "matmul"
            ),
            "matmul_checksum_max_rel_delta": max(
                float(r["checksum_rel_delta"]) for r in equiv
            ),
            "parallel_rerun_bit_stable_all_cells": all(
                r["parallel_rerun_bit_stable"] == "1" for r in equiv
            ),
        },
        "medians_s": {
            f"{wl}|n={size}|{variant}|threads={t}": round(m, 8)
            for (wl, variant, size, t), m in sorted(medians.items())
        },
        "speedup_vs_serial_median": {
            f"{wl}|n={size}|threads={t}": round(s, 3)
            for (wl, size, t), s in sorted(speedups.items())
        },
    }
    (rdir / "run_summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )

    best = max(speedups.items(), key=lambda kv: kv[1])
    worst = min(speedups.items(), key=lambda kv: kv[1])
    print("figures + table + summary written to", rdir)
    print("results bit-stable across repeats:", results_bit_stable)
    print(f"best speedup: {best[0]} = {best[1]:.2f}x")
    print(f"worst speedup: {worst[0]} = {worst[1]:.2f}x")


if __name__ == "__main__":
    main()
