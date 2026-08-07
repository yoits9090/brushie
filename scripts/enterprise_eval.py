#!/usr/bin/env python3
"""Enterprise-oriented, equal-quality evaluation for CAPS.

The research sweeps in this repository compare convenient codec quality
settings. Enterprise buyers instead care about the smallest representation
that reaches the same quality threshold on their workload. This script:

* routes photo and synthetic chat/UI samples through preview and expanded
  profiles;
* sweeps CAPS, JPEG, WebP, and AVIF when available;
* selects the smallest candidate meeting each MS-SSIM proxy threshold;
* accounts for the CAPS directory plus payload bytes needed by each
  progressive layer; and
* writes machine-readable CSV files and a short decision report.

The MS-SSIM value is the repository's deterministic proxy, not the canonical
windowed MS-SSIM implementation. LPIPS and human preference testing remain
required before making perceptual-quality claims to customers.
"""
from __future__ import annotations

import argparse
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
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "brushie"
THRESHOLDS = (0.970, 0.985, 0.995)
PREVIEW_THRESHOLDS = (0.950, 0.970)


def metrics(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    x, y = a.astype(np.float64), b.astype(np.float64)
    mse = float(np.mean((x - y) ** 2))
    psnr = 99.0 if mse == 0 else 10.0 * math.log10(255.0**2 / mse)

    def ssim_one(u: np.ndarray, v: np.ndarray) -> float:
        mu_u, mu_v = u.mean(), v.mean()
        var_u, var_v = u.var(), v.var()
        covariance = np.mean((u - mu_u) * (v - mu_v))
        c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
        return float(
            (2 * mu_u * mu_v + c1) * (2 * covariance + c2)
            / ((mu_u * mu_u + mu_v * mu_v + c1) * (var_u + var_v + c2))
        )

    current_a, current_b = a, b
    scores: list[float] = []
    for _ in range(4):
        scores.append(
            float(
                np.mean(
                    [
                        ssim_one(current_a[..., channel], current_b[..., channel])
                        for channel in range(3)
                    ]
                )
            )
        )
        if min(current_a.shape[:2]) < 4:
            break
        even_height = current_a.shape[0] & ~1
        even_width = current_a.shape[1] & ~1
        current_a = current_a[:even_height, :even_width]
        current_b = current_b[:even_height, :even_width]
        current_a = (
            current_a[0::2, 0::2].astype(np.float64)
            + current_a[1::2, 0::2]
            + current_a[0::2, 1::2]
            + current_a[1::2, 1::2]
        ) / 4
        current_b = (
            current_b[0::2, 0::2].astype(np.float64)
            + current_b[1::2, 0::2]
            + current_b[0::2, 1::2]
            + current_b[1::2, 1::2]
        ) / 4
    return psnr, float(np.prod(np.asarray(scores) ** (1 / len(scores))))


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


def source_images(photo_limit: int) -> list[tuple[str, str, Image.Image]]:
    result: list[tuple[str, str, Image.Image]] = []
    paths = sorted((ROOT / "datasets/kodak/PhotoCD_PCD0992").glob("*.png"))
    for path in paths[:photo_limit]:
        result.append((f"photo_{path.stem}", "photo", Image.open(path).convert("RGB")))
    result.append(("chat_ui", "ui_text", synthetic_chat()))
    result.append(("meme_card", "ui_text", synthetic_meme()))
    return result


def profiles(quick: bool) -> tuple[tuple[str, int, int], ...]:
    if quick:
        return (("chat_preview", 512, 512),)
    return (
        ("chat_preview", 512, 512),
        ("expanded", 1536, 1536),
    )


def caps_prefix_bytes(path: Path) -> dict[int, int]:
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"CAPS":
        raise ValueError(f"{path} is not a CAPS stream")
    levels = struct.unpack_from("<H", data, 16)[0]
    chunk_count = struct.unpack_from("<I", data, 32)[0]
    directory_bytes = struct.unpack_from("<I", data, 36)[0]
    data_offset = struct.unpack_from("<Q", data, 40)[0]
    if directory_bytes != chunk_count * 40 or data_offset != 64 + directory_bytes:
        raise ValueError(f"{path} has invalid directory accounting")
    payload_by_layer = [0] * (levels + 1)
    for index in range(chunk_count):
        offset = 64 + index * 40
        layer = struct.unpack_from("<H", data, offset)[0]
        payload_bytes = struct.unpack_from("<I", data, offset + 28)[0]
        if layer > levels:
            raise ValueError(f"{path} has an invalid progressive layer")
        payload_by_layer[layer] += payload_bytes
    result: dict[int, int] = {}
    cumulative = data_offset
    for layer, payload_bytes in enumerate(payload_by_layer):
        cumulative += payload_bytes
        result[layer] = cumulative
    if result[levels] != len(data):
        raise ValueError(f"{path} byte accounting does not reach EOF")
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
) -> dict[str, object]:
    psnr, ms_ssim = metrics(original, reconstructed)
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
        "psnr": psnr,
        "ms_ssim_proxy": ms_ssim,
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
    for quality in (5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 82, 85, 90):
        for tile in (64,):
            encoded = temp / f"{stem}.caps-q{quality}-t{tile}.caps"
            output = temp / f"{stem}.caps-q{quality}-t{tile}.ppm"
            encode = subprocess.run(
                [
                    str(CLI),
                    "encode",
                    str(ppm),
                    str(encoded),
                    str(quality),
                    "8",
                    str(tile),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
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
            rows.append(
                candidate(
                    "CAPS",
                    f"q={quality},tile={tile}",
                    encoded,
                    original,
                    reconstructed,
                    parse_stat(encode.stdout, "encode_ms"),
                    parse_stat(decode.stdout, "decode_ms"),
                    sample,
                    "resident codec timing; CLI file I/O excluded",
                )
            )
            if quality != 82 or tile != 64:
                continue
            prefix = caps_prefix_bytes(encoded)
            for layer, prefix_size in prefix.items():
                layer_output = temp / f"{stem}.caps-preview-l{layer}.ppm"
                layer_decode = subprocess.run(
                    [
                        str(CLI),
                        "decode",
                        str(encoded),
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
                layer_psnr, layer_ms_ssim = metrics(original, layer_array)
                progressive_rows.append(
                    {
                        **sample,
                        "quality": quality,
                        "tile": tile,
                        "layer": layer,
                        "prefix_bytes": prefix_size,
                        "full_bytes": encoded.stat().st_size,
                        "fraction_of_full": prefix_size / encoded.stat().st_size,
                        "decode_ms": parse_stat(layer_decode.stdout, "decode_ms"),
                        "psnr": layer_psnr,
                        "ms_ssim_proxy": layer_ms_ssim,
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
    for quality in qualities:
        encoded = temp / f"{stem}.{suffix}-q{quality}.{suffix}"
        start = time.perf_counter()
        image.save(encoded, format=fmt, quality=quality)
        encode_ms = (time.perf_counter() - start) * 1000
        start = time.perf_counter()
        reconstructed = decoded_array(encoded)
        decode_ms = (time.perf_counter() - start) * 1000
        rows.append(
            candidate(
                codec,
                f"q={quality}",
                encoded,
                original,
                reconstructed,
                encode_ms,
                decode_ms,
                sample,
                "Pillow wall time",
            )
        )


def run_avif(
    ffmpeg: str,
    temp: Path,
    stem: str,
    ppm: Path,
    original: np.ndarray,
    sample: dict[str, object],
    rows: list[dict[str, object]],
) -> None:
    for crf in (50, 40, 32, 24, 18):
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
                "-crf",
                str(crf),
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
        decode_ms = (time.perf_counter() - start) * 1000
        rows.append(
            candidate(
                "AVIF",
                f"crf={crf}",
                encoded,
                original,
                decoded_array(output),
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
                row for row in candidates if float(row["ms_ssim_proxy"]) >= threshold
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
                    "psnr": best["psnr"],
                    "ms_ssim_proxy": best["ms_ssim_proxy"],
                }
            )
    return result


def aggregate_matched(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, float, str], list[dict[str, object]]] = defaultdict(list)
    all_samples: dict[str, set[str]] = defaultdict(set)
    for row in rows:
        profile = str(row["profile"])
        all_samples[profile].add(str(row["sample"]))
        grouped[(profile, float(row["threshold"]), str(row["codec"]))].append(row)
    result: list[dict[str, object]] = []
    for (profile, threshold, codec), candidates in sorted(grouped.items()):
        result.append(
            {
                "profile": profile,
                "threshold": threshold,
                "codec": codec,
                "coverage": len(candidates),
                "samples": len(all_samples[profile]),
                "mean_bytes": float(np.mean([int(row["bytes"]) for row in candidates])),
                "mean_bpp": float(np.mean([float(row["bpp"]) for row in candidates])),
                "mean_encode_ms": float(
                    np.mean([float(row["encode_ms"]) for row in candidates])
                ),
                "mean_decode_ms": float(
                    np.mean([float(row["decode_ms"]) for row in candidates])
                ),
                "mean_ms_ssim_proxy": float(
                    np.mean([float(row["ms_ssim_proxy"]) for row in candidates])
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
                row for row in candidates if float(row["ms_ssim_proxy"]) >= threshold
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
                    "layer": best["layer"],
                    "prefix_bytes": best["prefix_bytes"],
                    "full_bytes": best["full_bytes"],
                    "fraction_of_full": best["fraction_of_full"],
                    "decode_ms": best["decode_ms"],
                    "psnr": best["psnr"],
                    "ms_ssim_proxy": best["ms_ssim_proxy"],
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
        "quality claim. MS-SSIM values use the repository's deterministic proxy;",
        "LPIPS and blinded human preference testing are still required.",
        "",
        f"Evaluated {candidate_count} encoded candidates.",
        "",
        "## Equal-quality aggregate",
        "",
        "| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |",
        "|---|---:|---|---:|---:|---:|---:|---:|",
    ]
    for row in aggregate:
        lines.append(
            f"| {row['profile']} | {float(row['threshold']):.3f} | "
            f"{row['codec']} | {row['coverage']}/{row['samples']} | "
            f"{float(row['mean_bytes']):.0f} | {float(row['mean_bpp']):.3f} | "
            f"{float(row['mean_encode_ms']):.2f} | "
            f"{float(row['mean_decode_ms']):.2f} |"
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
            "Do not claim an infrastructure-cost advantage until CAPS beats the",
            "smallest standard codec at full coverage and the same quality gate.",
            "Its progressive path should be evaluated separately on bytes and time",
            "to first useful preview.",
            "",
        ]
    )
    output.write_text("\n".join(lines), encoding="utf-8")


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

    ffmpeg = shutil.which("ffmpeg")
    candidates: list[dict[str, object]] = []
    progressive: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="brushie-enterprise-") as directory:
        temp = Path(directory)
        for source_name, category, source in source_images(args.photo_limit):
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
                    (35, 50, 65, 75, 82, 90, 95),
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
                    (35, 50, 65, 75, 82, 90, 95),
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

    prefix = args.output_prefix
    prefix.parent.mkdir(parents=True, exist_ok=True)
    matched = matched_quality(candidates)
    aggregate = aggregate_matched(matched)
    previews = preview_summary(progressive)
    write_csv(prefix.with_name(prefix.name + "_candidates.csv"), candidates)
    write_csv(prefix.with_name(prefix.name + "_matched.csv"), matched)
    write_csv(prefix.with_name(prefix.name + "_aggregate.csv"), aggregate)
    write_csv(prefix.with_name(prefix.name + "_progressive.csv"), progressive)
    write_csv(prefix.with_name(prefix.name + "_previews.csv"), previews)
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
