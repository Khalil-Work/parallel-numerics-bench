#!/usr/bin/env python3
"""Animated GIF from the RECORDED MC-pi progress data.

Every frame is one recorded checkpoint of docs/results/progress.csv
(written by `pnb_bench --mode progress`: 60 checkpoints of 1,000,000
samples each, serial run). Nothing is interpolated or synthesized: the
convergence you see is the data that was measured, at the sample counts
that were measured.

Style policy: pure white interiors, no grid lines.

Usage:
  python scripts/make_gif.py [--results docs/results] [--out mcpi_convergence.gif]
"""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import io  # noqa: E402

import matplotlib.pyplot as plt  # noqa: E402
from PIL import Image  # noqa: E402

PI = 3.14159265358979323846
LINE = "#1f4e79"
PI_LINE = "#c0504d"
ERR = "#4f8a4f"


def style_axis(ax):
    ax.set_facecolor("white")
    ax.grid(False)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color("#333333")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="docs/results")
    ap.add_argument("--out", default="mcpi_convergence.gif")
    args = ap.parse_args()
    rdir = Path(args.results)

    rows = []
    with open(rdir / "progress.csv", newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append(
                (int(r["checkpoint"]), int(r["samples_cum"]),
                 float(r["estimate"]), float(r["abs_err"]))
            )
    if len(rows) < 30 or len(rows) > 80:
        raise SystemExit(
            f"progress.csv has {len(rows)} checkpoints; the animation "
            "contract is 30-80 recorded frames"
        )

    frames = []
    for i in range(len(rows)):
        upto = rows[: i + 1]
        samples = [r[1] for r in upto]
        est = [r[2] for r in upto]
        err = [r[3] for r in upto]
        cp, ns, cur_est, cur_err = upto[-1]

        fig, (ax1, ax2) = plt.subplots(
            2, 1, figsize=(7.6, 5.0), sharex=True, facecolor="white"
        )
        ax1.axhline(PI, color=PI_LINE, linewidth=1.4, linestyle="--")
        ax1.plot(samples, est, color=LINE, linewidth=1.9)
        ax1.scatter([ns], [cur_est], color=LINE, s=26, zorder=3)
        ax1.set_xscale("log")
        ax1.set_ylabel("running pi estimate")
        ax1.set_ylim(
            PI - max(1.6 * max(r[3] for r in rows), 1e-3),
            PI + max(1.6 * max(r[3] for r in rows), 1e-3),
        )
        ax1.set_title(
            f"Monte Carlo pi convergence - checkpoint {cp}/60, "
            f"n = {ns:,} samples, estimate = {cur_est:.6f}",
            fontsize=10.5,
        )
        ax1.text(
            0.99, 0.06, "dashed: pi", transform=ax1.transAxes, ha="right",
            fontsize=8, color=PI_LINE,
        )
        style_axis(ax1)

        ax2.plot(samples, err, color=ERR, linewidth=1.9)
        ax2.scatter([ns], [cur_err], color=ERR, s=26, zorder=3)
        ax2.set_xscale("log")
        ax2.set_yscale("log")
        ax2.set_xlabel("samples (cumulative, recorded checkpoints)")
        ax2.set_ylabel("|estimate - pi|")
        ax2.text(
            0.99, 0.06, f"error = {cur_err:.2e}", transform=ax2.transAxes,
            ha="right", fontsize=8, color=ERR,
        )
        style_axis(ax2)

        fig.tight_layout()
        buf = io.BytesIO()
        fig.savefig(buf, format="png", dpi=110, facecolor="white")
        plt.close(fig)
        buf.seek(0)
        frames.append(Image.open(buf).convert("P", palette=Image.ADAPTIVE))

    out = rdir / args.out
    frames[0].save(
        out,
        save_all=True,
        append_images=frames[1:],
        duration=130,
        loop=0,
        optimize=True,
    )
    print(f"{out}: {len(frames)} frames from {len(rows)} recorded checkpoints")


if __name__ == "__main__":
    main()
