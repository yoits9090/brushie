# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 2923 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 59.83 | 24.42 |
| chat_preview | 0.970 | CAPS | 4/4 | 11346 | 0.520 | 12.10 | 9.52 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 0.85 | 0.54 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 37.27 | 24.27 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 10.56 | 0.88 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 63.60 | 25.87 |
| chat_preview | 0.985 | CAPS | 4/4 | 19351 | 0.887 | 8.32 | 8.23 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 0.96 | 0.59 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 43.68 | 27.28 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 14.00 | 1.17 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 64.61 | 25.46 |
| chat_preview | 0.995 | CAPS | 4/4 | 77869 | 3.568 | 10.29 | 10.55 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 2.24 | 1.27 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 43.81 | 29.53 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 68.27 | 1.63 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 7177 | 0.798 |
| chat_preview | 0.970 | 4 | 11010 | 0.911 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
