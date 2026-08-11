#!/usr/bin/env python3
"""Enterprise-oriented, equal-quality evaluation for CAPS.

The research sweeps in this repository compare convenient codec quality
settings. Enterprise buyers instead care about the smallest representation
that reaches the same quality threshold on their workload. This script:

* routes photo and synthetic chat/UI samples through preview and expanded
  profiles;
* sweeps CAPS, JPEG, WebP, and AVIF when available;
* selects the smallest candidate meeting each windowed MS-SSIM threshold;
* accounts for the CAPS directory plus payload bytes needed by each
  progressive layer; and
* writes machine-readable CSV files and a short decision report.

The primary quality gate is a local-window multiscale SSIM implementation;
the old global-moment proxy is retained only for historical comparison. LPIPS and human preference testing remain
required before making perceptual-quality claims to customers.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import platform
import sys
import csv
import math
import re
import shutil
import struct
import subprocess
import tempfile
import time
from collections import defaultdict
from pathlib import Path
from typing import Iterable

import numpy as np
import PIL
from PIL import Image, ImageDraw

from quality_metrics import METRIC_ID, evaluate as evaluate_quality


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "brushie"
THRESHOLDS = (0.970, 0.985, 0.995)
PREVIEW_THRESHOLDS = (0.950, 0.970)


def metrics(a: np.ndarray, b: np.ndarray) -> dict[str, float]:
    m = evaluate_quality(a, b)
    return {
        "psnr": m.psnr,
        "ssim_windowed": m.ssim_windowed,
        "ms_ssim_windowed": m.ms_ssim_windowed,
        "ms_ssim_luma": m.ms_ssim_luma,
        "legacy_ms_ssim_proxy": m.legacy_ms_ssim_proxy,
    }

def parse_stat(text: str, key: str) -> float:
    match = re.search(rf"{re.escape(key)}=([0-9.]+)", text)
    return float(match.group(1)) if match else float("nan")


def ppm_bytes(rgb: np.ndarray) -> bytes:
    height, width = rgb.shape[:2]
    return f"P6\n{width} {height}\n255\n".encode() + rgb.tobytes()


def fit(image: Image.Image, max_width: int, max_height: int) -> Image.Image:
    result = image.copy()
    result.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
    return result.convert("RGB")


def synthetic_chat(width: int = 1200, height: int = 800) -> Image.Image:
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
        draw.text(
            (308, y + 18),
            "A compressed attachment should stay sharp and load quickly.",
            fill=(190, 192, 198),
        )
    for index, label in enumerate(("general", "images", "design", "support", "random")):
        draw.text((24, 100 + index * 42), f"#  {label}", fill=(183, 185, 190))
    return image


def synthetic_meme(width: int = 1200, height: int = 800) -> Image.Image:
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


def source_images(
    kodak_limit: int, div2k_limit: int
) -> list[tuple[str, str, Image.Image]]:
    result: list[tuple[str, str, Image.Image]] = []
    kodak = sorted((ROOT / "datasets/kodak/PhotoCD_PCD0992").glob("*.png"))
    for path in kodak[:kodak_limit]:
        result.append((f"kodak_{path.stem}", "photo", Image.open(path).convert("RGB")))
    div2k = sorted((ROOT / "datasets/div2k").rglob("*.png"))
    for path in div2k[:div2k_limit]:
        result.append((f"div2k_{path.stem}", "photo", Image.open(path).convert("RGB")))
    result.append(("chat_ui", "ui_text", synthetic_chat()))
    result.append(("meme_card", "ui_text", synthetic_meme()))
    return result


def profiles(quick: bool) -> tuple[tuple[str, int, int], ...]:
    if quick:
        return (("chat_preview", 512, 512),)
    return (
        ("chat_preview", 512, 512),
        ("native_expanded", 1536, 1536),
    )


def caps_prefix_bytes(path: Path) -> dict[int, int]:
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"CAPS":
        raise ValueError(f"{path} is not a CAPS stream")
    version = struct.unpack_from("<H", data, 4)[0]
    entry_bytes = 16 if version >= 5 else (20 if version >= 3 else 40)
    levels = struct.unpack_from("<H", data, 16)[0]
    chunk_count = struct.unpack_from("<I", data, 32)[0]
    directory_bytes = struct.unpack_from("<I", data, 36)[0]
    data_offset = struct.unpack_from("<Q", data, 40)[0]
    if directory_bytes != chunk_count * entry_bytes or data_offset != 64 + directory_bytes:
        raise ValueError(f"{path} has invalid directory accounting")
    payload_by_layer = [0] * (levels + 1)
    expected_payload_offset = data_offset
    previous_layer = 0
    for index in range(chunk_count):
        entry = 64 + index * entry_bytes
        if version >= 3:
            layer = data[entry]
            payload_offset = expected_payload_offset
            payload_bytes = struct.unpack_from("<I", data, entry + 12)[0]
        else:
            layer = struct.unpack_from("<H", data, entry)[0]
            payload_offset = struct.unpack_from("<Q", data, entry + 20)[0]
            payload_bytes = struct.unpack_from("<I", data, entry + 28)[0]
        if layer > levels or layer < previous_layer:
            raise ValueError(f"{path} has non-prefix progressive layer order")
        if payload_offset != expected_payload_offset:
            raise ValueError(f"{path} payloads are not contiguous in directory order")
        if payload_offset + payload_bytes > len(data):
            raise ValueError(f"{path} payload exceeds EOF")
        payload_by_layer[layer] += payload_bytes
        expected_payload_offset += payload_bytes
        previous_layer = layer
    if expected_payload_offset != len(data):
        raise ValueError(f"{path} byte accounting does not reach EOF")
    result: dict[int, int] = {}
    cumulative = data_offset
    for layer, payload_bytes in enumerate(payload_by_layer):
        cumulative += payload_bytes
        result[layer] = cumulative
    return result

def decoded_array(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        image.load()
        return np.asarray(image.convert("RGB"), dtype=np.uint8)


def candidate(
    codec: str,
    setting: str,
    encoded: Path,
    original: np.ndarray,
    reconstructed: np.ndarray,
    encode_ms: float,
    decode_ms: float,
    sample: dict[str, object],
    notes: str,
    codec_encode_ms: float = float("nan"),
    codec_decode_ms: float = float("nan"),
) -> dict[str, object]:
    quality = metrics(original, reconstructed)
    height, width = original.shape[:2]
    size = encoded.stat().st_size
    return {
        **sample,
        "codec": codec,
        "setting": setting,
        "bytes": size,
        "bpp": size * 8 / (width * height),
        "encode_ms": encode_ms,
        "decode_ms": decode_ms,
        "codec_encode_ms": codec_encode_ms,
        "codec_decode_ms": codec_decode_ms,
        **quality,
        "notes": notes,
    }


def run_caps(
    temp: Path,
    stem: str,
    ppm: Path,
    original: np.ndarray,
    sample: dict[str, object],
    rows: list[dict[str, object]],
    progressive_rows: list[dict[str, object]],
) -> None:
    height, width = original.shape[:2]
    # Tile size is irrelevant in the v2 whole-band format; sweep quality only.
    for quality in range(1, 101):
        for tile, base_target in ((64, 32), (64, 64)):
            encoded = temp / f"{stem}.brbr-q{quality}-t{tile}-b{base_target}.brbr"
            output = temp / f"{stem}.brbr-q{quality}-t{tile}-b{base_target}.ppm"
            start = time.perf_counter()
            encode = subprocess.run(
                [
                    str(CLI),
                    "encode",
                    str(ppm),
                    str(encoded),
                    str(quality),
                    "8",
                    str(tile),
                    "--base-target",
                    str(base_target),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            encode_e2e_ms = (time.perf_counter() - start) * 1000
            start = time.perf_counter()
            decode = subprocess.run(
                [
                    str(CLI),
                    "decode",
                    str(encoded),
                    str(output),
                    str(width),
                    str(height),
                    "-1",
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            reconstructed = decoded_array(output)
            decode_e2e_ms = (time.perf_counter() - start) * 1000
            rows.append(
                candidate(
                    "CAPS",
                    f"q={quality},tile={tile},base={base_target}",
                    encoded,
                    original,
                    reconstructed,
                    encode_e2e_ms,
                    decode_e2e_ms,
                    sample,
                    "subprocess wall time including PPM I/O; codec_* fields are resident timing",
                    codec_encode_ms=parse_stat(encode.stdout, "encode_ms"),
                    codec_decode_ms=parse_stat(decode.stdout, "decode_ms"),
                )
            )
            # Sweep every quality×layer so the progressive result is on the
            # actual byte/quality Pareto frontier; a fixed q80 prefix can be
            # larger than an entire lower-quality file at the same gate.
            prefix = caps_prefix_bytes(encoded)
            encoded_data = encoded.read_bytes()
            for layer, prefix_size in prefix.items():
                # Decode the actual truncated physical prefix, not the full
                # file with a logical layer limit. This validates deployable
                # progressive streaming bytes.
                prefix_stream = temp / f"{stem}.q{quality}.b{base_target}.brbr-prefix-l{layer}.brbr"
                prefix_stream.write_bytes(encoded_data[:prefix_size])
                layer_output = temp / f"{stem}.q{quality}.b{base_target}.brbr-preview-l{layer}.ppm"
                layer_decode = subprocess.run(
                    [
                        str(CLI),
                        "decode",
                        str(prefix_stream),
                        str(layer_output),
                        str(width),
                        str(height),
                        str(layer),
                    ],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                layer_array = decoded_array(layer_output)
                layer_quality = metrics(original, layer_array)
                progressive_rows.append(
                    {
                        **sample,
                        "quality": quality,
                        "tile": tile,
                        "base_target": base_target,
                        "layer": layer,
                        "prefix_bytes": prefix_size,
                        "full_bytes": encoded.stat().st_size,
                        "fraction_of_full": prefix_size / encoded.stat().st_size,
                        "decode_ms": parse_stat(layer_decode.stdout, "decode_ms"),
                        **layer_quality,
                    }
                )


def run_pillow_codec(
    codec: str,
    fmt: str,
    qualities: Iterable[int],
    temp: Path,
    stem: str,
    image: Image.Image,
    original: np.ndarray,
    sample: dict[str, object],
    rows: list[dict[str, object]],
) -> None:
    suffix = "jpg" if codec == "JPEG" else "webp"
    if codec == "JPEG":
        modes: list[tuple[str, dict[str, object]]] = [
            ("seq420", {"optimize": True, "progressive": False, "subsampling": 2}),
            ("prog420", {"optimize": True, "progressive": True, "subsampling": 2}),
            ("seq444", {"optimize": True, "progressive": False, "subsampling": 0}),
            ("prog444", {"optimize": True, "progressive": True, "subsampling": 0}),
        ]
    else:
        modes = [("m6", {"method": 6, "exact": True})]
    for quality in qualities:
        for mode, mode_options in modes:
            encoded = temp / f"{stem}.{suffix}-{mode}-q{quality}.{suffix}"
            save_options: dict[str, object] = {"quality": quality, **mode_options}
            start = time.perf_counter()
            image.save(encoded, format=fmt, **save_options)
            encode_ms = (time.perf_counter() - start) * 1000
            start = time.perf_counter()
            reconstructed = decoded_array(encoded)
            decode_ms = (time.perf_counter() - start) * 1000
            rows.append(
                candidate(
                    codec,
                    f"{mode},q={quality}",
                    encoded,
                    original,
                    reconstructed,
                    encode_ms,
                    decode_ms,
                    sample,
                    "Pillow wall time including file I/O",
                )
            )
    if codec == "WebP":
        # Lossy WebP cannot reach some high-quality/chroma gates; include the
        # format's lossless mode as a valid endpoint.
        encoded = temp / f"{stem}.webp-lossless.webp"
        start = time.perf_counter()
        image.save(encoded, format="WEBP", lossless=True, method=6, exact=True)
        encode_ms = (time.perf_counter() - start) * 1000
        start = time.perf_counter()
        reconstructed = decoded_array(encoded)
        decode_ms = (time.perf_counter() - start) * 1000
        rows.append(
            candidate(
                "WebP",
                "lossless,m6",
                encoded,
                original,
                reconstructed,
                encode_ms,
                decode_ms,
                sample,
                "Pillow wall time including file I/O",
            )
        )

def run_jpeg2000(
    ffmpeg: str,
    temp: Path,
    stem: str,
    ppm: Path,
    original: np.ndarray,
    sample: dict[str, object],
    rows: list[dict[str, object]],
) -> None:
    """Adaptively bracket native-FFmpeg JPEG 2000 at every primary gate.

    q:v is an integer quantizer scale (larger means smaller/lower quality), so
    a fixed sparse list can miss a gate by orders of magnitude. We binary-search
    the largest scale that still reaches each gate, caching every actual encode.
    Both 4:2:0 and RGB 4:4:4 modes compete as format-level alternatives.
    """
    for pix_fmt in ("yuv420p", "rgb24"):
        cache: dict[int, dict[str, object]] = {}

        def evaluate_qscale(qscale: int) -> dict[str, object]:
            if qscale in cache:
                return cache[qscale]
            encoded = temp / f"{stem}.j2k-{pix_fmt}-{qscale}.jp2"
            output = temp / f"{stem}.j2k-{pix_fmt}-{qscale}.jp2.ppm"
            start = time.perf_counter()
            subprocess.run(
                [
                    ffmpeg,
                    "-y", "-loglevel", "error",
                    "-i", str(ppm),
                    "-frames:v", "1",
                    "-c:v", "jpeg2000",
                    "-pix_fmt", pix_fmt,
                    "-tile_width", str(original.shape[1]),
                    "-tile_height", str(original.shape[0]),
                    "-q:v", str(qscale),
                    str(encoded),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            encode_ms = (time.perf_counter() - start) * 1000
            start = time.perf_counter()
            subprocess.run(
                [
                    ffmpeg,
                    "-y", "-loglevel", "error",
                    "-i", str(encoded),
                    "-f", "image2", "-pix_fmt", "rgb24",
                    str(output),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            reconstructed = decoded_array(output)
            decode_ms = (time.perf_counter() - start) * 1000
            row = candidate(
                "JPEG2000",
                f"pix={pix_fmt},qscale={qscale}",
                encoded,
                original,
                reconstructed,
                encode_ms,
                decode_ms,
                sample,
                "native ffmpeg jpeg2000 wall time including process and file I/O",
            )
            cache[qscale] = row
            rows.append(row)
            return row

        low_q, high_q = 1, 10000
        low = evaluate_qscale(low_q)
        high = evaluate_qscale(high_q)
        for threshold in THRESHOLDS:
            if float(low["ms_ssim_windowed"]) < threshold:
                continue  # even the finest native setting cannot cover it
            if float(high["ms_ssim_windowed"]) >= threshold:
                continue  # the smallest endpoint already covers it
            lo, hi = low_q, high_q
            while hi - lo > 1:
                mid = (lo + hi) // 2
                point = evaluate_qscale(mid)
                if float(point["ms_ssim_windowed"]) >= threshold:
                    lo = mid
                else:
                    hi = mid
            # Include immediate neighbors so the matched-quality selector can
            # tolerate small non-monotonic score/size behavior.
            for q in range(max(low_q, lo - 2), min(high_q, hi + 2) + 1):
                evaluate_qscale(q)

def run_avif(
    ffmpeg: str,
    temp: Path,
    stem: str,
    ppm: Path,
    original: np.ndarray,
    sample: dict[str, object],
    rows: list[dict[str, object]],
) -> None:
    for crf in range(63, 0, -1):
        encoded = temp / f"{stem}.crf{crf}.avif"
        output = temp / f"{stem}.crf{crf}.avif.ppm"
        start = time.perf_counter()
        subprocess.run(
            [
                ffmpeg,
                "-y",
                "-loglevel",
                "error",
                "-i",
                str(ppm),
                "-frames:v",
                "1",
                "-c:v",
                "libsvtav1",
                "-preset",
                "4",
                "-crf",
                str(crf),
                "-svtav1-params",
                "avif=1:tune=0:lp=8",
                "-pix_fmt",
                "yuv420p",
                "-f",
                "avif",
                str(encoded),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        encode_ms = (time.perf_counter() - start) * 1000
        start = time.perf_counter()
        subprocess.run(
            [
                ffmpeg,
                "-y",
                "-loglevel",
                "error",
                "-i",
                str(encoded),
                "-f",
                "image2",
                "-pix_fmt",
                "rgb24",
                str(output),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        reconstructed = decoded_array(output)
        decode_ms = (time.perf_counter() - start) * 1000
        rows.append(
            candidate(
                "AVIF",
                f"crf={crf}",
                encoded,
                original,
                reconstructed,
                encode_ms,
                decode_ms,
                sample,
                "ffmpeg/libsvtav1 wall time including process and file I/O",
            )
        )


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def matched_quality(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        key = (str(row["profile"]), str(row["sample"]), str(row["codec"]))
        grouped[key].append(row)
    result: list[dict[str, object]] = []
    for (profile, sample_name, codec), candidates in sorted(grouped.items()):
        for threshold in THRESHOLDS:
            eligible = [
                row for row in candidates if float(row["ms_ssim_windowed"]) >= threshold
            ]
            if not eligible:
                continue
            best = min(
                eligible,
                key=lambda row: (int(row["bytes"]), float(row["encode_ms"])),
            )
            result.append(
                {
                    "profile": profile,
                    "category": best["category"],
                    "sample": sample_name,
                    "width": best["width"],
                    "height": best["height"],
                    "threshold": threshold,
                    "codec": codec,
                    "setting": best["setting"],
                    "bytes": best["bytes"],
                    "bpp": best["bpp"],
                    "encode_ms": best["encode_ms"],
                    "decode_ms": best["decode_ms"],
                    "codec_encode_ms": best["codec_encode_ms"],
                    "codec_decode_ms": best["codec_decode_ms"],
                    "psnr": best["psnr"],
                    "ssim_windowed": best["ssim_windowed"],
                    "ms_ssim_windowed": best["ms_ssim_windowed"],
                    "ms_ssim_luma": best["ms_ssim_luma"],
                    "legacy_ms_ssim_proxy": best["legacy_ms_ssim_proxy"],
                }
            )
    return result


def aggregate_matched(
    rows: list[dict[str, object]], candidates: list[dict[str, object]]
) -> list[dict[str, object]]:
    grouped: dict[tuple[str, float, str], list[dict[str, object]]] = defaultdict(list)
    all_samples: dict[str, set[str]] = defaultdict(set)
    codecs_by_profile: dict[str, set[str]] = defaultdict(set)
    for candidate in candidates:
        profile = str(candidate["profile"])
        all_samples[profile].add(str(candidate["sample"]))
        codecs_by_profile[profile].add(str(candidate["codec"]))
    for row in rows:
        grouped[(str(row["profile"]), float(row["threshold"]), str(row["codec"]))].append(row)

    result: list[dict[str, object]] = []
    for profile in sorted(all_samples):
        sample_count = len(all_samples[profile])
        for threshold in THRESHOLDS:
            for codec in sorted(codecs_by_profile[profile]):
                points = grouped.get((profile, float(threshold), codec), [])
                coverage = len(points)
                full = coverage == sample_count
                diagnostic_bytes = (
                    float(np.mean([int(row["bytes"]) for row in points])) if points else float("nan")
                )
                diagnostic_bpp = (
                    float(np.mean([float(row["bpp"]) for row in points])) if points else float("nan")
                )
                result.append(
                    {
                        "profile": profile,
                        "threshold": threshold,
                        "codec": codec,
                        "coverage": coverage,
                        "samples": sample_count,
                        "full_coverage": full,
                        # Primary aggregate fields are intentionally blank for
                        # partial coverage: survivor means are non-comparable.
                        "mean_bytes": diagnostic_bytes if full else float("nan"),
                        "mean_bpp": diagnostic_bpp if full else float("nan"),
                        "diagnostic_partial_mean_bytes": diagnostic_bytes,
                        "diagnostic_partial_mean_bpp": diagnostic_bpp,
                        "mean_encode_ms": (
                            float(np.mean([float(row["encode_ms"]) for row in points]))
                            if full else float("nan")
                        ),
                        "mean_decode_ms": (
                            float(np.mean([float(row["decode_ms"]) for row in points]))
                            if full else float("nan")
                        ),
                        "diagnostic_partial_encode_ms": (
                            float(np.mean([float(row["encode_ms"]) for row in points]))
                            if points else float("nan")
                        ),
                        "diagnostic_partial_decode_ms": (
                            float(np.mean([float(row["decode_ms"]) for row in points]))
                            if points else float("nan")
                        ),
                        "mean_ms_ssim_windowed": (
                            float(np.mean([float(row["ms_ssim_windowed"]) for row in points]))
                            if full else float("nan")
                        ),
                        "mean_legacy_ms_ssim_proxy": (
                            float(np.mean([float(row["legacy_ms_ssim_proxy"]) for row in points]))
                            if points else float("nan")
                        ),
                    }
                )
    return result

def preview_summary(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[(str(row["profile"]), str(row["sample"]))].append(row)
    result: list[dict[str, object]] = []
    for (profile, sample_name), candidates in sorted(grouped.items()):
        for threshold in PREVIEW_THRESHOLDS:
            eligible = [
                row for row in candidates if float(row["ms_ssim_windowed"]) >= threshold
            ]
            if not eligible:
                continue
            best = min(eligible, key=lambda row: int(row["prefix_bytes"]))
            result.append(
                {
                    "profile": profile,
                    "category": best["category"],
                    "sample": sample_name,
                    "width": best["width"],
                    "height": best["height"],
                    "preview_threshold": threshold,
                    "quality": best["quality"],
                    "tile": best["tile"],
                    "base_target": best["base_target"],
                    "layer": best["layer"],
                    "prefix_bytes": best["prefix_bytes"],
                    "full_bytes": best["full_bytes"],
                    "fraction_of_full": best["fraction_of_full"],
                    "decode_ms": best["decode_ms"],
                    "psnr": best["psnr"],
                    "ssim_windowed": best["ssim_windowed"],
                    "ms_ssim_windowed": best["ms_ssim_windowed"],
                    "ms_ssim_luma": best["ms_ssim_luma"],
                    "legacy_ms_ssim_proxy": best["legacy_ms_ssim_proxy"],
                }
            )
    return result


def markdown_report(
    aggregate: list[dict[str, object]],
    previews: list[dict[str, object]],
    output: Path,
    candidate_count: int,
) -> None:
    lines = [
        "# CAPS enterprise evaluation",
        "",
        "This is a directional engineering evaluation, not a customer-facing",
        "quality claim. The primary gate is local-window multiscale SSIM;",
        "LPIPS and blinded human preference testing are still required.",
        "",
        f"Metric: `{METRIC_ID}`.",
        f"Evaluated {candidate_count} encoded candidates.",
        "",
        "## Equal-quality aggregate",
        "",
        "| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |",
        "|---|---:|---|---:|---:|---:|---:|---:|",
    ]
    for row in aggregate:
        full = bool(row["full_coverage"])
        mean_bytes = f"{float(row['mean_bytes']):.0f}" if full else "— (partial)"
        mean_bpp = f"{float(row['mean_bpp']):.3f}" if full else "—"
        encode_ms = f"{float(row['mean_encode_ms']):.2f}" if full else "—"
        decode_ms = f"{float(row['mean_decode_ms']):.2f}" if full else "—"
        lines.append(
            f"| {row['profile']} | {float(row['threshold']):.3f} | "
            f"{row['codec']} | {row['coverage']}/{row['samples']} | "
            f"{mean_bytes} | {mean_bpp} | {encode_ms} | {decode_ms} |"
        )
    lines.extend(
        [
            "",
            "## CAPS progressive preview",
            "",
            "| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    preview_groups: dict[tuple[str, float], list[dict[str, object]]] = defaultdict(list)
    for row in previews:
        preview_groups[
            (str(row["profile"]), float(row["preview_threshold"]))
        ].append(row)
    for (profile, threshold), rows in sorted(preview_groups.items()):
        lines.append(
            f"| {profile} | {threshold:.3f} | {len(rows)} | "
            f"{np.mean([int(row['prefix_bytes']) for row in rows]):.0f} | "
            f"{np.mean([float(row['fraction_of_full']) for row in rows]):.3f} |"
        )
    lines.extend(
        [
            "",
            "## Decision rule",
            "",
            "Rows without full coverage are diagnostic only and cannot be compared",
            "to full-coverage codecs. Do not claim an infrastructure-cost advantage",
            "until CAPS beats the smallest standard codec at full coverage and the",
            "same quality gate.",
            "Its progressive path should be evaluated separately on bytes and time",
            "to first useful preview.",
            "",
        ]
    )
    output.write_text("\n".join(lines), encoding="utf-8")


def _first_line(command: list[str]) -> str:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        text = result.stdout or result.stderr
        return text.splitlines()[0] if text else "unavailable"
    except Exception:
        return "unavailable"


def capture_git_state() -> tuple[str, bool]:
    try:
        sha = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        dirty = bool(
            subprocess.run(
                ["git", "-C", str(ROOT), "status", "--porcelain"],
                capture_output=True, text=True, check=True,
            ).stdout.strip()
        )
        return sha, dirty
    except Exception:
        return "unavailable", True


def write_manifest(
    path: Path,
    args: argparse.Namespace,
    candidates: list[dict[str, object]],
    ffmpeg: str | None,
    git_sha: str,
    git_dirty: bool,
) -> None:
    samples: dict[tuple[str, str], dict[str, object]] = {}
    for row in candidates:
        key = (str(row["profile"]), str(row["sample"]))
        samples[key] = {
            "profile": row["profile"],
            "sample": row["sample"],
            "category": row["category"],
            "source_width": row["source_width"],
            "source_height": row["source_height"],
            "evaluated_width": row["width"],
            "evaluated_height": row["height"],
        }
    manifest = {
        "harness": "enterprise-eval-v3-windowed",
        "metric_id": METRIC_ID,
        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "command": sys.argv,
        "git_sha": git_sha,
        "git_dirty": git_dirty,
        "python": platform.python_version(),
        "platform": platform.platform(),
        "numpy": np.__version__,
        "pillow": PIL.__version__,
        "ffmpeg": _first_line([ffmpeg, "-version"]) if ffmpeg else None,
        "brushie_cli": str(CLI),
        "thresholds": THRESHOLDS,
        "preview_thresholds": PREVIEW_THRESHOLDS,
        "quick": args.quick,
        "kodak_limit": args.photo_limit,
        "div2k_limit": args.div2k_limit,
        "candidate_count": len(candidates),
        "samples": [samples[k] for k in sorted(samples)],
        "codec_configs": {
            "CAPS": "quality 1..100, base target 32/64, 8 threads, whole-band v2",
            "JPEG": "Pillow quality 1..100; optimized; sequential/progressive; 4:2:0 and 4:4:4",
            "WebP": "Pillow quality 1..100 method=6 exact=True plus lossless endpoint",
            "AVIF": "ffmpeg libsvtav1 CRF 1..63; preset=4; avif=1:tune=0:lp=8; yuv420p",
            "JPEG2000": "native ffmpeg jpeg2000; yuv420p+rgb24; whole-image tile; qscale sweep",
        },
        "timing_scope": "wall time including file I/O; process startup applies to CLI codecs",
    }
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--quick",
        action="store_true",
        help="Use two photos and the 512px chat-preview profile only.",
    )
    parser.add_argument(
        "--photo-limit",
        type=int,
        default=6,
        help="Number of Kodak photos to route through each profile.",
    )
    parser.add_argument(
        "--div2k-limit",
        type=int,
        default=4,
        help="Number of available DIV2K validation images (native <=1020px) to include.",
    )
    parser.add_argument(
        "--output-prefix",
        type=Path,
        default=ROOT / "enterprise_eval",
        help="Path prefix for CSV and Markdown outputs.",
    )
    args = parser.parse_args()
    if not CLI.exists():
        raise SystemExit("build brushie first")
    if args.quick:
        args.photo_limit = min(args.photo_limit, 2)
        args.div2k_limit = 0

    git_sha, git_dirty = capture_git_state()
    ffmpeg = shutil.which("ffmpeg")
    candidates: list[dict[str, object]] = []
    progressive: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="brushie-enterprise-") as directory:
        temp = Path(directory)
        for source_name, category, source in source_images(args.photo_limit, args.div2k_limit):
            source_width, source_height = source.size
            for profile, max_width, max_height in profiles(args.quick):
                image = fit(source, max_width, max_height)
                original = np.asarray(image, dtype=np.uint8)
                height, width = original.shape[:2]
                stem = f"{source_name}-{profile}"
                ppm = temp / f"{stem}.ppm"
                ppm.write_bytes(ppm_bytes(original))
                sample: dict[str, object] = {
                    "profile": profile,
                    "category": category,
                    "sample": source_name,
                    "source_width": source_width,
                    "source_height": source_height,
                    "width": width,
                    "height": height,
                }
                run_caps(
                    temp,
                    stem,
                    ppm,
                    original,
                    sample,
                    candidates,
                    progressive,
                )
                run_pillow_codec(
                    "JPEG",
                    "JPEG",
                    tuple(range(1, 101)),
                    temp,
                    stem,
                    image,
                    original,
                    sample,
                    candidates,
                )
                run_pillow_codec(
                    "WebP",
                    "WEBP",
                    tuple(range(1, 101)),
                    temp,
                    stem,
                    image,
                    original,
                    sample,
                    candidates,
                )
                if ffmpeg:
                    run_avif(
                        ffmpeg,
                        temp,
                        stem,
                        ppm,
                        original,
                        sample,
                        candidates,
                    )
                    run_jpeg2000(
                        ffmpeg,
                        temp,
                        stem,
                        ppm,
                        original,
                        sample,
                        candidates,
                    )

    prefix = args.output_prefix
    prefix.parent.mkdir(parents=True, exist_ok=True)
    matched = matched_quality(candidates)
    aggregate = aggregate_matched(matched, candidates)
    previews = preview_summary(progressive)
    write_csv(prefix.with_name(prefix.name + "_candidates.csv"), candidates)
    write_csv(prefix.with_name(prefix.name + "_matched.csv"), matched)
    write_csv(prefix.with_name(prefix.name + "_aggregate.csv"), aggregate)
    write_csv(prefix.with_name(prefix.name + "_progressive.csv"), progressive)
    write_csv(prefix.with_name(prefix.name + "_previews.csv"), previews)
    write_manifest(
        prefix.with_name(prefix.name + "_manifest.json"), args, candidates, ffmpeg,
        git_sha, git_dirty
    )
    markdown_report(
        aggregate,
        previews,
        prefix.with_name(prefix.name + "_report.md"),
        len(candidates),
    )
    print(
        f"wrote {len(candidates)} candidates, {len(matched)} matched points, "
        f"and {len(progressive)} progressive points to {prefix.parent}"
    )


if __name__ == "__main__":
    main()
