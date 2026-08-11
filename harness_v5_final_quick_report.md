# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 51.72 | 19.66 |
| chat_preview | 0.970 | CAPS | 4/4 | 9120 | 0.418 | 8.13 | 7.23 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 0.87 | 0.48 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 32.13 | 21.08 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 10.47 | 0.97 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 53.23 | 20.10 |
| chat_preview | 0.985 | CAPS | 4/4 | 16162 | 0.741 | 8.81 | 7.49 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 0.92 | 0.59 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 38.84 | 22.69 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 13.90 | 1.16 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 54.06 | 21.97 |
| chat_preview | 0.995 | CAPS | 4/4 | 35380 | 1.621 | 11.12 | 8.64 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 2.22 | 1.24 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 39.15 | 27.41 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 68.04 | 1.56 |

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
