#!/usr/bin/env python3
"""Corpus-wide byte-share audit: where do the bytes live per gate?

Encodes every broad-corpus image at q=30/50/75, audits each v5 stream, and
aggregates payload+directory bytes by (layer, band, channel).
Output: campaign/headroom.csv.
"""
from __future__ import annotations
import csv, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from byte_audit import audit

CLI = ROOT / "build" / "brushie"
CORPUS = ROOT / "benchmarks" / "corpus.txt"
OUT = ROOT / "campaign" / "headroom.csv"

def main():
    from PIL import Image
    images = [l.strip() for l in CORPUS.read_text().splitlines() if l.strip() and not l.startswith("#")]
    rows = []
    t0 = time.time()
    for i, img in enumerate(images):
        im = Image.open(img).convert("RGB")
        im.thumbnail((512, 512), Image.Resampling.LANCZOS)
        with tempfile.TemporaryDirectory() as td:
            ppm = Path(td) / "in.ppm"
            im.save(ppm)
            for q in (30, 50, 75):
                stream = Path(td) / f"q{q}.brbr"
                r = subprocess.run([str(CLI), "encode", str(ppm), str(stream), "--quality", str(q)],
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    print("ENC FAIL", img, q, r.stderr[-200:]); continue
                for row in audit(stream):
                    rows.append({
                        "image": Path(img).stem, "q": q,
                        "layer": row["layer"], "band": row["band"], "channel": row["channel"],
                        "payload_bytes": row["payload_bytes"],
                        "directory_bytes": row["directory_bytes"],
                        "coefficients": row["coefficients"], "nonzero": row["nonzero"],
                        "total_stream_bytes": stream.stat().st_size,
                    })
        if (i + 1) % 20 == 0:
            print(f"{i+1}/{len(images)} in {time.time()-t0:.0f}s", flush=True)
    OUT.parent.mkdir(exist_ok=True)
    with open(OUT, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    print("wrote", OUT, len(rows), "rows in", round(time.time()-t0, 1), "s")

if __name__ == "__main__":
    main()
