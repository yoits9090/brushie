#!/usr/bin/env python3
"""Benchmark CAPS and available Pillow baselines on Kodak/DIV2K samples.

PNG decoding and resize happen before the timed CAPS call. The CLI starts its
timer only after the resident PPM buffer has been read, matching the API
contract rather than conflating disk loading with encoding.
"""
from __future__ import annotations
import csv
import math
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PY = ROOT / ".benchmark_python"
CLI = ROOT / "build" / "brushie"
if not CLI.exists():
    raise SystemExit("build brushie first")


def metrics(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    x = a.astype(np.float64)
    y = b.astype(np.float64)
    mse = np.mean((x - y) ** 2)
    psnr = 99.0 if mse == 0 else 10.0 * math.log10(255.0**2 / mse)

    def ssim_one(u: np.ndarray, v: np.ndarray) -> float:
        mu_u, mu_v = u.mean(), v.mean()
        vu, vv = u.var(), v.var()
        cov = np.mean((u - mu_u) * (v - mu_v))
        c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
        return float((2 * mu_u * mu_v + c1) * (2 * cov + c2) /
                     ((mu_u * mu_u + mu_v * mu_v + c1) * (vu + vv + c2)))

    def gray(z: np.ndarray) -> np.ndarray:
        return z[..., 0] * 0.299 + z[..., 1] * 0.587 + z[..., 2] * 0.114

    def one_scale(u: np.ndarray, v: np.ndarray) -> float:
        vals = [ssim_one(u[..., c], v[..., c]) for c in range(3)]
        return float(np.mean(vals))

    cur_a, cur_b = a, b
    scores = []
    for _ in range(4):
        scores.append(one_scale(cur_a, cur_b))
        if min(cur_a.shape[:2]) < 4:
            break
        cur_a = (cur_a[0::2, 0::2].astype(np.float64) + cur_a[1::2, 0::2] +
                 cur_a[0::2, 1::2] + cur_a[1::2, 1::2]) / 4
        cur_b = (cur_b[0::2, 0::2].astype(np.float64) + cur_b[1::2, 0::2] +
                 cur_b[0::2, 1::2] + cur_b[1::2, 1::2]) / 4
    ms_ssim = float(np.prod(np.array(scores) ** (1 / len(scores))))
    return psnr, ms_ssim


def parse(text: str, name: str) -> float:
    m = re.search(rf"{re.escape(name)}=([0-9.]+)", text)
    return float(m.group(1)) if m else float("nan")


def ppm_bytes(rgb: np.ndarray) -> bytes:
    h, w = rgb.shape[:2]
    return f"P6\n{w} {h}\n255\n".encode() + rgb.tobytes()


def samples() -> list[tuple[str, Path]]:
    result: list[tuple[str, Path]] = []
    for p in sorted((ROOT / "datasets/kodak/PhotoCD_PCD0992").glob("*.png")):
        result.append((f"kodak_{p.stem}", p))
    div = sorted((ROOT / "datasets/div2k/DIV2K_valid_LR_bicubic/X2").glob("*.png"))
    for p in div[:10]:
        result.append((f"div2k_{p.stem}", p))
    return result


def main() -> None:
    rows = []
    progressive = []
    with tempfile.TemporaryDirectory(prefix="brushie-bench-") as td:
        temp = Path(td)
        for sample_name, source_path in samples():
            source = Image.open(source_path).convert("RGB")
            for n in (512, 1024):
                image = source.resize((n, n), Image.Resampling.LANCZOS)
                arr = np.asarray(image, dtype=np.uint8)
                ppm = temp / f"{sample_name}_{n}.ppm"
                ppm.write_bytes(ppm_bytes(arr))
                caps = temp / f"{sample_name}_{n}.caps"
                out = temp / f"{sample_name}_{n}.out.ppm"
                enc = subprocess.run([str(CLI), "encode", str(ppm), str(caps), "82", "8"], capture_output=True, text=True, check=True)
                dec = subprocess.run([str(CLI), "decode", str(caps), str(out), str(n), str(n), "-1"], capture_output=True, text=True, check=True)
                reconstructed = np.asarray(Image.open(out).convert("RGB"), dtype=np.uint8)
                psnr, ms = metrics(arr, reconstructed)
                rows.append({"codec": "CAPS", "image": sample_name, "size": n,
                             "quality": 82, "encode_ms": parse(enc.stdout, "encode_ms"),
                             "decode_ms": parse(dec.stdout, "decode_ms"), "bytes": caps.stat().st_size,
                             "bpp": caps.stat().st_size * 8 / (n * n), "psnr": psnr,
                             "ms_ssim": ms, "lpips": "", "notes": "8 threads"})
                levels = int(parse(enc.stdout, "levels"))
                for layer in range(levels + 1):
                    pout = temp / f"{sample_name}_{n}.layer{layer}.ppm"
                    pdec = subprocess.run([str(CLI), "decode", str(caps), str(pout), str(n), str(n), str(layer)], capture_output=True, text=True, check=True)
                    pa = np.asarray(Image.open(pout).convert("RGB"), dtype=np.uint8)
                    ppsnr, pms = metrics(arr, pa)
                    progressive.append({"image": sample_name, "size": n, "layer": layer,
                                        "decode_ms": parse(pdec.stdout, "decode_ms"),
                                        "psnr": ppsnr, "ms_ssim": pms})

                for codec, fmt, kwargs in (("PNG", "PNG", {}), ("JPEG", "JPEG", {"quality": 82}), ("WebP", "WEBP", {"quality": 82})):
                    t0 = time.perf_counter()
                    encoded = temp / f"{sample_name}_{n}.{fmt.lower()}"
                    image.save(encoded, format=fmt, **kwargs)
                    b = np.asarray(Image.open(encoded).convert("RGB"), dtype=np.uint8)
                    ms_enc = (time.perf_counter() - t0) * 1000
                    bpsnr, bms = metrics(arr, b)
                    rows.append({"codec": codec, "image": sample_name, "size": n,
                                 "quality": 82, "encode_ms": ms_enc, "decode_ms": "",
                                 "bytes": encoded.stat().st_size,
                                 "bpp": encoded.stat().st_size * 8 / (n * n), "psnr": bpsnr,
                                 "ms_ssim": bms, "lpips": "", "notes": "Pillow"})

                # ffmpeg is present on the reference host and exposes an AVIF
                # muxer with libsvtav1. Decode is measured separately; this is
                # a baseline measurement, not part of the CAPS timing gate.
                ffmpeg = shutil.which("ffmpeg")
                if ffmpeg:
                    encoded = temp / f"{sample_name}_{n}.avif"
                    t0 = time.perf_counter()
                    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(ppm),
                                    "-frames:v", "1", "-c:v", "libsvtav1", "-crf", "30",
                                    "-pix_fmt", "yuv420p", "-f", "avif", str(encoded)], check=True,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    avif_encode_ms = (time.perf_counter() - t0) * 1000
                    decoded_ppm = temp / f"{sample_name}_{n}.avif.ppm"
                    t0 = time.perf_counter()
                    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(encoded),
                                    "-f", "image2", "-pix_fmt", "rgb24", str(decoded_ppm)], check=True,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    avif_decode_ms = (time.perf_counter() - t0) * 1000
                    av = np.asarray(Image.open(decoded_ppm).convert("RGB"), dtype=np.uint8)
                    apsnr, ams = metrics(arr, av)
                    rows.append({"codec": "AVIF", "image": sample_name, "size": n,
                                 "quality": 30, "encode_ms": avif_encode_ms, "decode_ms": avif_decode_ms,
                                 "bytes": encoded.stat().st_size,
                                 "bpp": encoded.stat().st_size * 8 / (n * n), "psnr": apsnr,
                                 "ms_ssim": ams, "lpips": "", "notes": "ffmpeg/libsvtav1 CRF30"})

    with (ROOT / "benchmark_dataset_results.csv").open("w", newline="") as f:
        fields = ["codec", "image", "size", "quality", "encode_ms", "decode_ms", "bytes", "bpp", "psnr", "ms_ssim", "lpips", "notes"]
        writer = csv.DictWriter(f, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
    with (ROOT / "progressive_results.csv").open("w", newline="") as f:
        fields = ["image", "size", "layer", "decode_ms", "psnr", "ms_ssim"]
        writer = csv.DictWriter(f, fieldnames=fields); writer.writeheader(); writer.writerows(progressive)
    availability = [
        ("CAPS", CLI.exists(), "local build"),
        ("PNG", True, "Pillow"),
        ("JPEG", True, "Pillow"),
        ("WebP", True, "Pillow"),
        ("AVIF", bool(shutil.which("ffmpeg")), "ffmpeg/libsvtav1"),
        ("JXL", bool(shutil.which("cjxl")), "cjxl not installed" if not shutil.which("cjxl") else "cjxl"),
    ]
    with (ROOT / "baseline_availability.csv").open("w", newline="") as f:
        writer = csv.writer(f); writer.writerow(["codec", "available", "encoder"])
        writer.writerows((name, int(ok), note) for name, ok, note in availability)
    print(f"wrote {len(rows)} codec rows and {len(progressive)} progressive rows")


if __name__ == "__main__":
    main()
