#!/usr/bin/env python3
"""Diff two tracking runs: stage timings, per-chunk bytes, cycles."""
from __future__ import annotations
import argparse, csv, json, os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACK = ROOT / "tracking"


def load_timeline(tag: str) -> list[dict]:
    p = TRACK / tag / "timeline.csv"
    if not p.exists():
        return []
    return list(csv.DictReader(p.open()))


def stage_table(rows: list[dict]) -> dict[str, float]:
    out: dict[str, float] = {}
    for r in rows:
        key = r["phase"] + "/" + r["name"]
        out[key] = out.get(key, 0.0) + float(r["ns"] or 0)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    args = ap.parse_args()
    ta, tb = load_timeline(args.a), load_timeline(args.b)
    sa, sb = stage_table(ta), stage_table(tb)
    keys = sorted(set(sa) | set(sb))
    print(f"{'stage':<28} {args.a:>14} {args.b:>14} {'delta':>9}")
    for k in keys:
        a, b = sa.get(k, 0.0), sb.get(k, 0.0)
        d = (b - a) / a * 100 if a else 0.0
        print(f"{k:<28} {a/1e6:>11.2f}ms {b/1e6:>11.2f}ms {d:>+8.1f}%")
    # bytes
    pa, pb = TRACK / args.a / "bytes.csv", TRACK / args.b / "bytes.csv"
    if pa.exists() and pb.exists():
        def sums(p):
            tot = {}
            for row in csv.DictReader(p.open()):
                key = f"L{row['layer']}B{row['band']}C{row['channel']}"
                tot[key] = tot.get(key, 0) + int(row["payload_bytes"])
            return tot
        ba, bb = sums(pa), sums(pb)
        keys = sorted(set(ba) | set(bb))
        print(f"\n{'chunk':<12} {args.a:>12} {args.b:>12} {'delta':>9}")
        for k in keys:
            a, b = ba.get(k, 0), bb.get(k, 0)
            d = (b - a) / a * 100 if a else 0.0
            print(f"{k:<12} {a:>10} B {b:>10} B {d:>+8.1f}%")
    # cycles if present
    ca = sum(float(r["cycles"] or 0) for r in ta)
    cb = sum(float(r["cycles"] or 0) for r in tb)
    if ca or cb:
        print(f"\ncycles: {args.a}={ca:,.0f} {args.b}={cb:,.0f} "
              f"({(cb-ca)/ca*100 if ca else 0:+.1f}%)")


if __name__ == "__main__":
    main()
