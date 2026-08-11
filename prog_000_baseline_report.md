# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 101.56 | 38.17 |
| chat_preview | 0.970 | CAPS | 4/4 | 10325 | 0.473 | 13.70 | 14.03 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 1.74 | 0.99 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 67.02 | 44.02 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 20.28 | 1.76 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 105.99 | 38.67 |
| chat_preview | 0.985 | CAPS | 4/4 | 18316 | 0.839 | 14.91 | 15.74 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 1.85 | 1.09 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 77.51 | 46.48 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 29.59 | 2.20 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 111.97 | 47.83 |
| chat_preview | 0.995 | CAPS | 4/4 | 38174 | 1.749 | 18.08 | 17.29 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 4.27 | 2.47 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 80.98 | 57.03 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 130.75 | 3.03 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 6598 | 0.862 |
| chat_preview | 0.970 | 4 | 10234 | 0.959 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
