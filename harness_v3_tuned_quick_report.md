# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 73.09 | 28.41 |
| chat_preview | 0.970 | CAPS | 4/4 | 10770 | 0.493 | 8.79 | 9.22 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 1.12 | 0.59 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 41.64 | 28.54 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 13.90 | 1.25 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 73.68 | 27.56 |
| chat_preview | 0.985 | CAPS | 4/4 | 18817 | 0.862 | 10.60 | 13.01 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 1.07 | 0.62 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 52.31 | 31.32 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 17.87 | 1.42 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 77.11 | 30.18 |
| chat_preview | 0.995 | CAPS | 4/4 | 38709 | 1.774 | 10.59 | 10.94 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 2.56 | 1.43 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 54.41 | 36.14 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 101.34 | 1.92 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 7008 | 0.869 |
| chat_preview | 0.970 | 4 | 10679 | 0.966 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
