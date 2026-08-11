#!/usr/bin/env python3
"""Merge competitor cache + CAPS results into dash/broad.json for the dashboard."""
import csv, json, sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "benchmarks" / "competitors"
CAPS = ROOT / "benchmarks" / "caps_broad_profiled.csv"
THRESHOLDS = (0.970, 0.985, 0.995)

def load_rows(paths):
    rows = []
    for p in paths:
        if not p.exists():
            continue
        with p.open() as f:
            for r in csv.DictReader(f):
                try:
                    rows.append({**r, "bytes": int(r["bytes"]), "ms_ssim": float(r["ms_ssim"]),
                                 "encode_ms": float(r["encode_ms"]), "decode_ms": float(r["decode_ms"])})
                except (ValueError, KeyError):
                    pass
    return rows

comp_rows = []
for p in sorted(COMP.glob("*.csv")):
    for r in load_rows([p]):
        r["sample"] = p.stem
        comp_rows.append(r)
caps_rows = load_rows([ROOT / "benchmarks" / "caps_broad_profiled.csv"])
for r in caps_rows:
    r["sample"] = r["sample"]

by = defaultdict(list)
for r in comp_rows + caps_rows:
    by[(r["codec"], r["sample"])].append(r)

out = {"gates": {}, "samples": 0, "speeds": {}}
samples = sorted({r["sample"] for r in comp_rows + caps_rows})
out["samples"] = len(samples)
for th in THRESHOLDS:
    out["gates"][f"{th:.3f}"] = {}
    for codec in ["AVIF", "WebP", "JPEG", "JPEG2000", "CAPS"]:
        vals = []
        for s in samples:
            cands = [r for r in by.get((codec, s), []) if r["ms_ssim"] >= th]
            if cands:
                vals.append(min(c["bytes"] for c in cands))
        if vals:
            out["gates"][f"{th:.3f}"][codec] = {"mean_bytes": int(sum(vals)/len(vals)), "n": len(vals)}
# speeds: mean encode/decode per codec from caps + competitor rows (matched settings only)
sp = defaultdict(lambda: {"enc": [], "dec": []})
for r in comp_rows + caps_rows:
    sp[r["codec"]]["enc"].append(r["encode_ms"])
    sp[r["codec"]]["dec"].append(r["decode_ms"])
for codec, v in sp.items():
    if v["enc"]:
        out["speeds"][codec] = {"encode_ms": round(sum(v["enc"])/len(v["enc"]), 1),
                                "decode_ms": round(sum(v["dec"])/len(v["dec"]), 1),
                                "n": len(v["enc"])}
(ROOT / "dash" / "broad.json").write_text(json.dumps(out, indent=1))
print(json.dumps(out, indent=1)[:800])
