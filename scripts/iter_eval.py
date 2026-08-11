#!/usr/bin/env python3
"""Fast CAPS-only iteration eval.

Same corpus (2 Kodak photos + chat + meme at 512px), same windowed MS-SSIM
metric, same exhaustive q=1..100 x base-target sweep, same smallest-bytes
selector as scripts/enterprise_eval.py --quick, but only CAPS candidates so a
direction check takes ~1-2 minutes instead of ~19.
"""
from __future__ import annotations
import csv, json, subprocess, sys, tempfile, time
from pathlib import Path
import numpy as np
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parent))
from quality_metrics import evaluate as evaluate_quality

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "brushie"
THRESHOLDS = (0.970, 0.985, 0.995)

def fit(image, max_width, max_height):
    result = image.copy()
    result.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
    return result.convert("RGB")

def synthetic_chat(width=1200, height=800):
    from PIL import ImageDraw
    image = Image.new("RGB", (width, height), (49, 51, 56))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 235, height), fill=(43, 45, 49))
    draw.rectangle((235, 0, width, 58), fill=(54, 57, 63))
    draw.text((260, 20), "# product-images", fill=(245, 245, 245))
    colors = [(88, 101, 242), (235, 69, 158), (35, 165, 90), (250, 166, 26)]
    for index in range(13):
        y = 82 + index * 52
        avatar = colors[index % len(colors)]
        draw.ellipse((260, y, 294, y + 34), fill=avatar)
        draw.text((308, y), f"member-{index + 1}", fill=(245, 245, 245))
        draw.text((308, y + 18), "A compressed attachment should stay sharp and load quickly.", fill=(190, 192, 198))
    for index, label in enumerate(("general", "images", "design", "support", "random")):
        draw.text((24, 100 + index * 42), f"#  {label}", fill=(183, 185, 190))
    return image

def synthetic_meme(width=1200, height=800):
    from PIL import ImageDraw
    yy, xx = np.mgrid[:height, :width]
    red = (35 + 185 * xx / max(1, width - 1)).astype(np.uint8)
    green = (55 + 145 * yy / max(1, height - 1)).astype(np.uint8)
    blue = (165 + 50 * np.sin((xx + yy) / 75)).clip(0, 255).astype(np.uint8)
    image = Image.fromarray(np.stack([red, green, blue], axis=-1))
    draw = ImageDraw.Draw(image)
    draw.rectangle((55, 45, width - 55, 145), fill=(15, 15, 15))
    draw.rectangle((55, height - 145, width - 55, height - 45), fill=(15, 15, 15))
    draw.text((85, 80), "WHEN THE IMAGE IS SMALLER", fill=(255, 255, 255))
    draw.text((85, height - 110), "BUT THE LIKENESS SURVIVES", fill=(255, 255, 255))
    return image

def sources():
    kodak = sorted((ROOT / "datasets/kodak/PhotoCD_PCD0992").glob("*.png"))
    result = []
    for path in kodak[:2]:
        result.append((f"kodak_{path.stem}", Image.open(path).convert("RGB")))
    result.append(("chat_ui", synthetic_chat()))
    result.append(("meme_card", synthetic_meme()))
    return result

def run(tag=None, quality_range=range(1, 101), base_targets=(32, 64), quiet=False):
    rows = []
    with tempfile.TemporaryDirectory(prefix="brushie-iter-") as d:
        temp = Path(d)
        for name, source in sources():
            image = fit(source, 512, 512)
            orig = np.asarray(image, dtype=np.uint8)
            h, w = orig.shape[:2]
            ppm = temp / f"{name}.ppm"
            ppm.write_bytes(f"P6\n{w} {h}\n255\n".encode() + orig.tobytes())
            for q in quality_range:
                for base in base_targets:
                    enc = temp / f"{name}-q{q}-b{base}.brbr"
                    dec = temp / f"{name}-q{q}-b{base}.ppm"
                    subprocess.run([str(CLI), "encode", str(ppm), str(enc), str(q), "8", "64", "--base-target", str(base)],
                                   capture_output=True, text=True, check=True)
                    subprocess.run([str(CLI), "decode", str(enc), str(dec), str(w), str(h), "-1"],
                                   capture_output=True, text=True, check=True)
                    rec = np.asarray(Image.open(dec).convert("RGB"), dtype=np.uint8)
                    m = evaluate_quality(orig, rec)
                    rows.append({"sample": name, "q": q, "base": base,
                                 "bytes": enc.stat().st_size,
                                 "ms_ssim": m.ms_ssim_windowed})
    best = {}
    for sample in sorted({r["sample"] for r in rows}):
        for th in THRESHOLDS:
            cands = [r for r in rows if r["sample"] == sample and r["ms_ssim"] >= th]
            if cands:
                b = min(cands, key=lambda r: r["bytes"])
                best[(sample, th)] = b
    result = {"tag": tag, "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
              "gates": {f"{th:.3f}": int(np.mean([best[(s, th)]["bytes"] for s in sorted({r['sample'] for r in rows}) if (s, th) in best])) for th in THRESHOLDS},
              "per_sample": {f"{s}|{th:.3f}": best[(s, th)]["bytes"] for s in sorted({r['sample'] for r in rows}) for th in THRESHOLDS if (s, th) in best},
              "samples": sorted({r["sample"] for r in rows})}
    if not quiet:
        print(json.dumps(result, indent=1))
    return result

if __name__ == "__main__":
    tag = sys.argv[1] if len(sys.argv) > 1 else None
    run(tag)
