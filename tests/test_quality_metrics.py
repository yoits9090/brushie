#!/usr/bin/env python3
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from quality_metrics import evaluate, windowed_ms_ssim_rgb, windowed_ssim_rgb


def assert_close(a: float, b: float, eps: float = 1e-9) -> None:
    assert abs(a - b) <= eps, (a, b)


def main() -> None:
    yy, xx = np.mgrid[:128, :160]
    rgb = np.stack(
        [
            (xx * 255 / 159),
            (yy * 255 / 127),
            (127 + 80 * np.sin((xx + yy) / 9)),
        ],
        axis=-1,
    ).clip(0, 255).astype(np.uint8)
    identical = evaluate(rgb, rgb)
    assert_close(identical.psnr, 99.0)
    assert_close(identical.ssim_windowed, 1.0)
    assert_close(identical.ms_ssim_windowed, 1.0)
    assert_close(identical.ms_ssim_luma, 1.0)

    # Local structural corruption: reverse each 16-column stripe. Global
    # moments barely change, but local edges/phase do; windowed metrics must
    # notice the damage.
    corrupt = rgb.copy()
    for x in range(0, rgb.shape[1], 16):
        corrupt[:, x:x + 16] = corrupt[:, x:x + 16, :][:, ::-1]
    damaged = evaluate(rgb, corrupt)
    assert damaged.ms_ssim_windowed < 0.995, damaged
    assert damaged.ssim_windowed < 0.99, damaged
    assert damaged.legacy_ms_ssim_proxy > damaged.ms_ssim_windowed, damaged
    # Stable in-repo reference vector for metric ID brushie-box11-ms-ssim-v1.
    assert abs(damaged.ssim_windowed - 0.48657176149869813) < 1e-10
    assert abs(damaged.ms_ssim_windowed - 0.7427722263808031) < 1e-10
    assert abs(damaged.ms_ssim_luma - 0.9548023964918083) < 1e-10

    # Tiny/odd images must not crash or produce NaN.
    for h, w in ((1, 1), (2, 2), (3, 2)):
        tiny = np.zeros((h, w, 3), dtype=np.uint8)
        changed = tiny.copy()
        changed[0, 0] = 255
        tm = evaluate(tiny, changed)
        assert 0.0 <= tm.ms_ssim_windowed <= 1.0, tm

    # More blur must not score above less blur.
    mild = (
        rgb.astype(np.uint16)
        + np.roll(rgb, 1, axis=0).astype(np.uint16)
        + np.roll(rgb, 1, axis=1).astype(np.uint16)
    ) // 3
    heavy = mild.copy()
    for _ in range(3):
        heavy = (
            heavy.astype(np.uint16)
            + np.roll(heavy, 1, axis=0).astype(np.uint16)
            + np.roll(heavy, 1, axis=1).astype(np.uint16)
        ) // 3
    assert windowed_ms_ssim_rgb(rgb, heavy.astype(np.uint8)) < windowed_ms_ssim_rgb(
        rgb, mild.astype(np.uint8)
    )
    assert windowed_ssim_rgb(rgb, heavy.astype(np.uint8)) < windowed_ssim_rgb(
        rgb, mild.astype(np.uint8)
    )
    print("quality metric tests passed")


if __name__ == "__main__":
    main()
