#!/usr/bin/env python3
"""Review a lab branch on a box: sync, build, run the quick harness, pull
the aggregate report, print the gate table vs frozen baselines.

Usage: python3 campaign/review.py <branch> <box> <lab> [--tag TAG]
The quick harness takes ~10-20 min on a 2-core Colab VM.
"""
from __future__ import annotations
import argparse, csv, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FROZEN = {"CAPS": {"0.970": 9120, "0.985": 16162, "0.995": 35380},
          "AVIF": {"0.970": 6466, "0.985": 12948, "0.995": 31810},
          "WebP": {"0.970": 7658, "0.985": 13901, "0.995": 65498},
          "JPEG": {"0.970": 10140, "0.985": 16328, "0.995": 32427}}


def sh(cmd, timeout=1800):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("branch")
    ap.add_argument("box")
    ap.add_argument("lab")
    ap.add_argument("--tag", default="review")
    args = ap.parse_args()
    remote = f"/content/labs/{args.lab}_{args.tag}"
    print(f"[1/4] syncing {args.branch} -> {args.box}:{remote}")
    r = sh(f"git archive {args.branch} | ssh {args.box} "
           f""mkdir -p {remote} && tar x -C {remote}"")
    if r.returncode: sys.exit(f"sync failed: {r.stderr[-300:]}")
    r = sh(f"ssh {args.box} "cd {remote} && ln -sfn /content/brushie/datasets datasets && "
           f"mkdir -p build && clang++ -std=c++17 -O3 -ffast-math -Iinclude "
           f"src/codec.cpp src/main.cpp -o build/brushie -pthread"")
    if r.returncode: sys.exit(f"build failed: {r.stderr[-300:]}")
    print(f"[2/4] running quick harness (this takes a while)...")
    r = sh(f"ssh {args.box} "cd {remote} && python3 -u scripts/enterprise_eval.py --quick "
           f"--output-prefix {remote}/review > {remote}/review.log 2>&1"", timeout=2400)
    if r.returncode: sys.exit(f"harness failed: {r.stderr[-800:]}")
    print("[3/4] pulling results")
    out = ROOT / "campaign" / f"review_{args.lab}_{args.tag}"
    out.mkdir(parents=True, exist_ok=True)
    for f in ["_aggregate.csv", "_report.md"]:
        cat = sh(f"ssh {args.box} cat {remote}/review{f}", timeout=300)
        if cat.returncode == 0:
            (out / f"review{f}").write_text(cat.stdout)
    print("[4/4] gate table vs frozen baselines")
    agg = out / "review_aggregate.csv"
    if not agg.exists():
        print("no aggregate produced; see", out, "and remote", remote + "/review.log")
        return
    rows = list(csv.DictReader(agg.open()))
    print(f"{'codec':<10} {'gate':<6} {'frozen':>8} {'this':>8} {'delta':>8}")
    seen = set()
    for row in rows:
        codec = row.get("codec", "?")
        gate = row.get("gate", row.get("quality", "?"))
        if gate not in ("0.970", "0.985", "0.995"):
            continue
        try:
            this = float(row["mean_bytes"])
        except (KeyError, ValueError):
            continue
        frozen = FROZEN.get(codec, {}).get(gate, None)
        if frozen is None:
            continue
        d = (this - frozen) / frozen * 100
        print(f"{codec:<10} {gate:<6} {frozen:>8,.0f} {this:>8,.0f} {d:>+7.1f}%")
        seen.add((codec, gate))
    if not seen:
        print("no matching rows; inspect", agg)
    print("artifacts:", out)


if __name__ == "__main__":
    main()
