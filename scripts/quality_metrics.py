#!/usr/bin/env python3
"""Dependency-light image quality metrics for the Brushie benchmark.

The old enterprise harness used one global mean/variance/covariance value per
channel and called the result "MS-SSIM". That is not windowed SSIM and can
reward severe blur. This module implements local-window SSIM and multiscale
SSIM with a fast integral-image box window (11x11 by default), plus the legacy
proxy for historical comparisons.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

import numpy as np

METRIC_ID = "brushie-box11-ms-ssim-v1"

_STANDARD_MS_WEIGHTS = np.asarray(
    [0.0448, 0.2856, 0.3001, 0.2363, 0.1333], dtype=np.float64
)


@dataclass(frozen=True)
class QualityMetrics:
    psnr: float
    ssim_windowed: float
    ms_ssim_windowed: float
    ms_ssim_luma: float
    legacy_ms_ssim_proxy: float


def _validate(a: np.ndarray, b: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    if a.shape != b.shape:
        raise ValueError(f"metric shape mismatch: {a.shape} vs {b.shape}")
    if a.ndim != 3 or a.shape[2] < 3:
        raise ValueError(f"expected HxWx3+ image, got {a.shape}")
    return a[..., :3].astype(np.float64), b[..., :3].astype(np.float64)


def psnr(a: np.ndarray, b: np.ndarray) -> float:
    x, y = _validate(a, b)
    mse = float(np.mean((x - y) ** 2))
    return 99.0 if mse == 0.0 else 10.0 * math.log10(255.0**2 / mse)


def _box_mean_valid(a: np.ndarray, window: int) -> np.ndarray:
    """Fast valid-window box mean using an integral image."""
    h, w = a.shape
    window = min(window, h, w)
    if window < 1:
        raise ValueError("empty image")
    if window % 2 == 0 and window > 1:
        window -= 1
    integral = np.pad(a, ((1, 0), (1, 0)), mode="constant")
    integral = np.cumsum(np.cumsum(integral, axis=0), axis=1)
    sums = (
        integral[window:, window:]
        - integral[:-window, window:]
        - integral[window:, :-window]
        + integral[:-window, :-window]
    )
    return sums / float(window * window)


def _ssim_components(
    x: np.ndarray, y: np.ndarray, window: int = 11
) -> tuple[float, float, float]:
    """Return mean luminance, contrast-structure, and SSIM components."""
    mu_x = _box_mean_valid(x, window)
    mu_y = _box_mean_valid(y, window)
    ex2 = _box_mean_valid(x * x, window)
    ey2 = _box_mean_valid(y * y, window)
    exy = _box_mean_valid(x * y, window)
    vx = np.maximum(0.0, ex2 - mu_x * mu_x)
    vy = np.maximum(0.0, ey2 - mu_y * mu_y)
    cov = exy - mu_x * mu_y
    c1, c2 = (0.01 * 255.0) ** 2, (0.03 * 255.0) ** 2
    lum = (2.0 * mu_x * mu_y + c1) / (mu_x * mu_x + mu_y * mu_y + c1)
    cs = (2.0 * cov + c2) / (vx + vy + c2)
    # Numerical noise can produce tiny excursions; negative CS is a real
    # severe structural mismatch and is clamped for the geometric MS product.
    full = lum * cs
    return float(np.mean(lum)), float(np.mean(cs)), float(np.mean(full))


def windowed_ssim_rgb(a: np.ndarray, b: np.ndarray, window: int = 11) -> float:
    x, y = _validate(a, b)
    return float(
        np.mean([_ssim_components(x[..., c], y[..., c], window)[2] for c in range(3)])
    )


def _downsample2(a: np.ndarray) -> np.ndarray:
    if min(a.shape) < 2:
        return a
    # Reflect the final sample when dimensions are odd instead of silently
    # dropping the last row/column.
    pad_h = a.shape[0] & 1
    pad_w = a.shape[1] & 1
    if pad_h or pad_w:
        pad_width = [(0, pad_h), (0, pad_w)] + [(0, 0)] * (a.ndim - 2)
        a = np.pad(a, pad_width, mode="edge")
    return (
        a[0::2, 0::2]
        + a[1::2, 0::2]
        + a[0::2, 1::2]
        + a[1::2, 1::2]
    ) * 0.25


def _ms_ssim_channel(
    x: np.ndarray, y: np.ndarray, window: int = 11, max_scales: int = 5
) -> float:
    contrast_structure: list[float] = []
    full_ssim: list[float] = []
    cx, cy = x, y
    for scale in range(max_scales):
        if min(cx.shape) < 1:
            break
        _, cs, full = _ssim_components(cx, cy, window)
        contrast_structure.append(max(1e-12, cs))
        full_ssim.append(max(1e-12, full))
        if scale + 1 == max_scales or min(cx.shape) < 22:
            break
        cx, cy = _downsample2(cx), _downsample2(cy)
    if not full_ssim:
        return 1.0 if np.array_equal(x, y) else 0.0
    n = len(full_ssim)
    weights = _STANDARD_MS_WEIGHTS[:n].copy()
    weights /= weights.sum()
    # MS-SSIM form: CS at every scale except the last and the *mean SSIM
    # map* at the final scale. Do not multiply mean(luminance)*mean(CS), which
    # is not mean(luminance*CS).
    value = 1.0
    for i in range(n - 1):
        value *= contrast_structure[i] ** weights[i]
    value *= full_ssim[-1] ** weights[-1]
    return float(value)


def windowed_ms_ssim_rgb(
    a: np.ndarray, b: np.ndarray, window: int = 11, max_scales: int = 5
) -> float:
    x, y = _validate(a, b)
    return float(
        np.mean(
            [_ms_ssim_channel(x[..., c], y[..., c], window, max_scales) for c in range(3)]
        )
    )


def windowed_ms_ssim_luma(
    a: np.ndarray, b: np.ndarray, window: int = 11, max_scales: int = 5
) -> float:
    x, y = _validate(a, b)
    weights = np.asarray([0.2126, 0.7152, 0.0722], dtype=np.float64)
    lx = np.tensordot(x, weights, axes=([-1], [0]))
    ly = np.tensordot(y, weights, axes=([-1], [0]))
    return _ms_ssim_channel(lx, ly, window, max_scales)


def legacy_ms_ssim_proxy(a: np.ndarray, b: np.ndarray) -> float:
    """Historical global-moment proxy, retained only to compare old reports."""
    x, y = _validate(a, b)

    def global_ssim(u: np.ndarray, v: np.ndarray) -> float:
        mu_u, mu_v = u.mean(), v.mean()
        var_u, var_v = u.var(), v.var()
        covariance = np.mean((u - mu_u) * (v - mu_v))
        c1, c2 = (0.01 * 255.0) ** 2, (0.03 * 255.0) ** 2
        return float(
            (2 * mu_u * mu_v + c1)
            * (2 * covariance + c2)
            / ((mu_u * mu_u + mu_v * mu_v + c1) * (var_u + var_v + c2))
        )

    scores: list[float] = []
    while len(scores) < 4:
        scores.append(float(np.mean([global_ssim(x[..., c], y[..., c]) for c in range(3)])))
        if min(x.shape[:2]) < 4:
            break
        x, y = _downsample2(x), _downsample2(y)
    return float(np.prod(np.asarray(scores) ** (1.0 / len(scores))))


def evaluate(a: np.ndarray, b: np.ndarray) -> QualityMetrics:
    return QualityMetrics(
        psnr=psnr(a, b),
        ssim_windowed=windowed_ssim_rgb(a, b),
        ms_ssim_windowed=windowed_ms_ssim_rgb(a, b),
        ms_ssim_luma=windowed_ms_ssim_luma(a, b),
        legacy_ms_ssim_proxy=legacy_ms_ssim_proxy(a, b),
    )
