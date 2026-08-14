# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 108.73 | 42.72 |
| chat_preview | 0.970 | CAPS | 4/4 | 8400 | 0.385 | 18.87 | 15.83 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 2.20 | 1.56 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 65.42 | 44.22 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 23.35 | 1.99 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 112.42 | 43.37 |
| chat_preview | 0.985 | CAPS | 4/4 | 15264 | 0.699 | 20.45 | 15.44 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 1.78 | 0.95 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 79.27 | 53.93 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 34.30 | 2.15 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 113.74 | 48.07 |
| chat_preview | 0.995 | CAPS | 4/4 | 33966 | 1.556 | 28.44 | 18.07 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 4.51 | 2.38 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 79.59 | 54.72 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 133.35 | 3.17 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 5229 | 0.934 |
| chat_preview | 0.970 | 4 | 8359 | 0.978 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
