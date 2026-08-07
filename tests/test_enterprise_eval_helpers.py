#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import enterprise_eval as ee


def row(sample: str, codec: str, size: int, primary: float, legacy: float) -> dict[str, object]:
    return {
        "profile": "p",
        "category": "photo",
        "sample": sample,
        "width": 64,
        "height": 64,
        "codec": codec,
        "setting": "x",
        "bytes": size,
        "bpp": size * 8 / 4096,
        "encode_ms": 1.0,
        "decode_ms": 1.0,
        "codec_encode_ms": float("nan"),
        "codec_decode_ms": float("nan"),
        "psnr": 30.0,
        "ssim_windowed": primary,
        "ms_ssim_windowed": primary,
        "ms_ssim_luma": primary,
        "legacy_ms_ssim_proxy": legacy,
        "notes": "",
    }


def main() -> None:
    candidates = [
        # Legacy score says the tiny CAPS point passes; local MS-SSIM says no.
        row("a", "CAPS", 50, 0.80, 0.999),
        row("a", "CAPS", 100, 0.975, 0.90),
        row("a", "AVIF", 80, 0.98, 0.98),
        row("b", "CAPS", 110, 0.98, 0.98),
        row("b", "AVIF", 70, 0.90, 0.999),
    ]
    old = ee.THRESHOLDS
    ee.THRESHOLDS = (0.970,)
    try:
        matched = ee.matched_quality(candidates)
        caps_a = next(r for r in matched if r["codec"] == "CAPS" and r["sample"] == "a")
        assert caps_a["bytes"] == 100, caps_a
        # AVIF only covers sample a at the primary gate, so its aggregate must
        # be marked partial and its denominator must still be the full corpus.
        aggregate = ee.aggregate_matched(matched, candidates)
        avif = next(r for r in aggregate if r["codec"] == "AVIF")
        caps = next(r for r in aggregate if r["codec"] == "CAPS")
        assert (avif["coverage"], avif["samples"], avif["full_coverage"]) == (1, 2, False), avif
        assert (caps["coverage"], caps["samples"], caps["full_coverage"]) == (2, 2, True), caps
    finally:
        ee.THRESHOLDS = old
    print("enterprise eval helper tests passed")


if __name__ == "__main__":
    main()
