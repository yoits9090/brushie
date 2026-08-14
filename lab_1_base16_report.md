# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 155.69 | 52.81 |
| chat_preview | 0.970 | CAPS | 4/4 | 9116 | 0.418 | 19.06 | 18.12 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 2.27 | 1.07 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 67.39 | 41.77 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 31.63 | 2.29 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 135.86 | 57.87 |
| chat_preview | 0.985 | CAPS | 4/4 | 16143 | 0.740 | 39.75 | 40.29 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 2.32 | 1.77 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 100.13 | 62.94 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 82.39 | 27.69 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 138.83 | 51.38 |
| chat_preview | 0.995 | CAPS | 4/4 | 35352 | 1.620 | 31.11 | 24.28 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 11.57 | 4.41 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 120.51 | 70.36 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 286.65 | 3.53 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 5795 | 0.937 |
| chat_preview | 0.970 | 4 | 9072 | 0.983 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
