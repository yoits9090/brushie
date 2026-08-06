#!/usr/bin/env python3
"""Generate the frozen-corpus CAPS rate/distortion sweep.

The codec is invoked on resident PPM buffers through the CLI only to keep the
experiment reproducible. LPIPS is deliberately left blank when its runtime is
not installed; MS-SSIM is the deterministic four-scale proxy used elsewhere
in this repository and is never mislabeled as LPIPS.
"""
from __future__ import annotations
import csv, math, re, subprocess, tempfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "brushie"
if not CLI.exists():
    raise SystemExit("build brushie first")


def metrics(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    x, y = a.astype(np.float64), b.astype(np.float64)
    mse = float(np.mean((x - y) ** 2))
    psnr = 99.0 if mse == 0 else 10.0 * math.log10(255.0 ** 2 / mse)

    def one(u: np.ndarray, v: np.ndarray) -> float:
        mu, mv, vu, vv = u.mean(), v.mean(), u.var(), v.var()
        cov = np.mean((u - mu) * (v - mv))
        c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
        return float((2 * mu * mv + c1) * (2 * cov + c2) /
                     ((mu * mu + mv * mv + c1) * (vu + vv + c2)))

    def scale(u: np.ndarray, v: np.ndarray) -> float:
        return float(np.mean([one(u[..., c], v[..., c]) for c in range(3)]))

    aa, bb, scores = a, b, []
    for _ in range(4):
        scores.append(scale(aa, bb))
        if min(aa.shape[:2]) < 4:
            break
        aa = (aa[0::2, 0::2].astype(np.float64) + aa[1::2, 0::2] +
              aa[0::2, 1::2] + aa[1::2, 1::2]) / 4
        bb = (bb[0::2, 0::2].astype(np.float64) + bb[1::2, 0::2] +
              bb[0::2, 1::2] + bb[1::2, 1::2]) / 4
    return psnr, float(np.prod(np.asarray(scores) ** (1 / len(scores))))


def parse(text: str, key: str) -> float:
    m = re.search(rf"{re.escape(key)}=([0-9.]+)", text)
    return float(m.group(1)) if m else float("nan")


def samples() -> list[tuple[str, Path]]:
    out = [(f"kodak_{p.stem}", p) for p in
           sorted((ROOT / "datasets/kodak/PhotoCD_PCD0992").glob("*.png"))]
    out.extend((f"div2k_{p.stem}", p) for p in
               sorted((ROOT / "datasets/div2k/DIV2K_valid_LR_bicubic/X2").glob("*.png"))[:10])
    return out


def main() -> None:
    rows = []
    qualities = (100, 90, 82, 70, 50, 35, 20)
    with tempfile.TemporaryDirectory(prefix="brushie-rd-") as td:
        temp = Path(td)
        for name, path in samples():
            source = Image.open(path).convert("RGB")
            for size in (512, 1024):
                arr = np.asarray(source.resize((size, size), Image.Resampling.LANCZOS), dtype=np.uint8)
                ppm = temp / f"{name}-{size}.ppm"
                ppm.write_bytes(f"P6\n{size} {size}\n255\n".encode() + arr.tobytes())
                for quality in qualities:
                    for tile in (32, 64) if quality == 82 else (32,):
                        caps = temp / f"{name}-{size}-{quality}-{tile}.caps"
                        out = temp / f"{name}-{size}-{quality}-{tile}.ppm"
                        enc = subprocess.run([str(CLI), "encode", str(ppm), str(caps),
                                              str(quality), "8", str(tile)],
                                             capture_output=True, text=True, check=True)
                        dec = subprocess.run([str(CLI), "decode", str(caps), str(out),
                                              str(size), str(size), "-1"],
                                             capture_output=True, text=True, check=True)
                        rec = np.asarray(Image.open(out).convert("RGB"), dtype=np.uint8)
                        psnr, ms = metrics(arr, rec)
                        rows.append({
                            "codec": "CAPS", "image": name, "size": size,
                            "quality": quality, "tile": tile,
                            "encode_ms": parse(enc.stdout, "encode_ms"),
                            "decode_ms": parse(dec.stdout, "decode_ms"),
                            "bytes": caps.stat().st_size,
                            "bpp": caps.stat().st_size * 8 / (size * size),
                            "psnr": psnr, "ms_ssim_proxy": ms, "lpips": "",
                            "lpips_status": "unavailable",
                            "constraint_status": "unverified_lpips",
                        })
    fields = list(rows[0].keys())
    with (ROOT / "rate_distortion_results.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader(); writer.writerows(rows)
    print(f"wrote {len(rows)} rate-distortion rows")


if __name__ == "__main__":
    main()
