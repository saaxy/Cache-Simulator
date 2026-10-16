#!/usr/bin/env python3
"""Plot the CSV written by cachesim_bench (pandas + Matplotlib).

Usage:
    python scripts/plot_results.py results/sample_results.csv docs/img

Writes one PNG per experiment group into the output directory and prints a
summary table for each. Everything plotted is a result of the functional
simulator; AMAT values are model-based estimates, not hardware measurements.
"""
import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")  # no display needed
import matplotlib.pyplot as plt
import pandas as pd

# Display order of variants inside each experiment (the CSV is already in this order).
def ordered_variants(df):
    return list(dict.fromkeys(df["variant"]))


def short_trace(name):
    return name.replace("synthetic:", "")


def grouped_bars(df, value, ylabel, title, path, ylim=None, fmt="{:.2f}"):
    variants = ordered_variants(df)
    traces = list(dict.fromkeys(df["trace"]))
    width = 0.8 / len(traces)
    fig, ax = plt.subplots(figsize=(9, 4.5))
    for i, trace in enumerate(traces):
        sub = df[df["trace"] == trace].set_index("variant").loc[variants]
        positions = [x + i * width for x in range(len(variants))]
        bars = ax.bar(positions, sub[value], width, label=short_trace(trace))
        if len(variants) * len(traces) <= 24:
            for bar, v in zip(bars, sub[value]):
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), fmt.format(v),
                        ha="center", va="bottom", fontsize=6.5, rotation=90)
    ax.set_xticks([x + 0.4 - width / 2 for x in range(len(variants))])
    ax.set_xticklabels(variants, rotation=15 if max(map(len, variants)) > 8 else 0, ha="right"
                       if max(map(len, variants)) > 8 else "center")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if ylim:
        ax.set_ylim(ylim[0], ylim[1] + 0.2)  # headroom so the legend never covers a bar
    ax.legend(title="trace", fontsize=8, ncol=len(traces), loc="upper center")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def lines(df, value, xlabel, ylabel, title, path, ylim=None):
    variants = ordered_variants(df)
    fig, ax = plt.subplots(figsize=(8, 4.5))
    for trace in dict.fromkeys(df["trace"]):
        sub = df[df["trace"] == trace].set_index("variant").loc[variants]
        ax.plot(range(len(variants)), sub[value], marker="o", label=short_trace(trace))
    ax.set_xticks(range(len(variants)))
    ax.set_xticklabels(variants)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if ylim:
        ax.set_ylim(*ylim)
    ax.legend(title="trace", fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", help="results CSV produced by cachesim_bench")
    parser.add_argument("outdir", help="directory for the PNG files")
    args = parser.parse_args()

    df = pd.read_csv(args.csv)
    required = {"experiment", "variant", "trace", "hit_rate", "amat_cycles_estimate"}
    missing = required - set(df.columns)
    if missing:
        sys.exit(f"{args.csv} is missing columns: {sorted(missing)} (was it written by cachesim_bench?)")
    os.makedirs(args.outdir, exist_ok=True)
    pd.set_option("display.width", 200)
    pd.set_option("display.max_columns", 20)

    def out(name):
        return os.path.join(args.outdir, name)

    groups = set(df["experiment"])

    if "associativity" in groups:
        d = df[df["experiment"] == "associativity"]
        grouped_bars(d, "hit_rate", "hit rate", "Hit rate vs associativity (same capacity, LRU)",
                     out("associativity.png"), ylim=(0, 1.08))
        print("\nHit rate by associativity\n", d.pivot(index="variant", columns="trace", values="hit_rate").loc[ordered_variants(d)].round(4))

    if "replacement" in groups:
        d = df[df["experiment"] == "replacement"]
        grouped_bars(d, "hit_rate", "hit rate", "LRU vs FIFO (4-way)", out("replacement.png"), ylim=(0, 1.08))
        print("\nHit rate by replacement policy\n", d.pivot(index="variant", columns="trace", values="hit_rate").loc[ordered_variants(d)].round(4))

    if "capacity" in groups:
        d = df[df["experiment"] == "capacity"]
        lines(d, "hit_rate", "cache capacity", "hit rate", "Hit rate vs capacity (4-way, 64 B blocks)",
              out("capacity.png"), ylim=(-0.02, 1.05))
        print("\nHit rate by capacity\n", d.pivot(index="variant", columns="trace", values="hit_rate").loc[ordered_variants(d)].round(4))

    if "block_size" in groups:
        d = df[df["experiment"] == "block_size"]
        lines(d, "hit_rate", "block size (bytes)", "hit rate", "Hit rate vs block size (fixed capacity)",
              out("block_size.png"), ylim=(-0.02, 1.05))
        print("\nHit rate by block size\n", d.pivot(index="variant", columns="trace", values="hit_rate").loc[ordered_variants(d)].round(4))

    if "write_policy" in groups:
        d = df[df["experiment"] == "write_policy"].copy()
        d["memory_transactions_per_access"] = (d["memory_reads"] + d["memory_writes"]) / d["accesses"]
        grouped_bars(d, "memory_transactions_per_access", "memory transactions per access",
                     "Traffic to memory by write policy (transactions, not bytes)",
                     out("write_policy.png"), fmt="{:.3f}")
        print("\nMemory transactions per access by write policy\n",
              d.pivot(index="variant", columns="trace", values="memory_transactions_per_access").loc[ordered_variants(d)].round(4))

    if "l2_size" in groups:
        d = df[df["experiment"] == "l2_size"]
        lines(d, "amat_cycles_estimate", "L2 size ('no-L2' = single level)", "estimated AMAT (cycles, model-based)",
              "Estimated AMAT vs L2 size (illustrative latencies 4 / 12 / 200 cycles)", out("l2_amat.png"))
        print("\nEstimated AMAT (cycles) by L2 size\n", d.pivot(index="variant", columns="trace", values="amat_cycles_estimate").loc[ordered_variants(d)].round(2))

    print(f"\nPNG files written to {args.outdir}")


if __name__ == "__main__":
    main()
