# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 139.00 | 50.16 |
| chat_preview | 0.970 | CAPS | 4/4 | 9120 | 0.418 | 19.92 | 18.37 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 2.08 | 1.21 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 71.01 | 48.79 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 33.11 | 2.03 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 123.10 | 46.85 |
| chat_preview | 0.985 | CAPS | 4/4 | 16162 | 0.741 | 18.21 | 20.31 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 2.66 | 1.08 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 85.98 | 51.33 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 36.66 | 2.66 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 194.36 | 75.92 |
| chat_preview | 0.995 | CAPS | 4/4 | 35381 | 1.621 | 39.32 | 23.03 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 9.29 | 2.80 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 94.05 | 65.61 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 143.36 | 3.27 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 5796 | 0.937 |
| chat_preview | 0.970 | 4 | 9074 | 0.983 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
