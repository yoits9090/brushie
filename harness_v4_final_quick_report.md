# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 52.64 | 20.47 |
| chat_preview | 0.970 | CAPS | 4/4 | 9516 | 0.436 | 7.94 | 8.45 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 0.86 | 0.47 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 32.32 | 21.19 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 10.49 | 0.96 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 53.20 | 20.39 |
| chat_preview | 0.985 | CAPS | 4/4 | 16311 | 0.747 | 8.69 | 8.82 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 0.95 | 0.52 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 38.77 | 23.07 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 14.45 | 1.20 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 55.01 | 22.69 |
| chat_preview | 0.995 | CAPS | 4/4 | 35534 | 1.628 | 12.28 | 10.99 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 2.37 | 1.72 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 39.00 | 26.58 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 69.79 | 1.55 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 5905 | 0.901 |
| chat_preview | 0.970 | 4 | 9484 | 0.984 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
