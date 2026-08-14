# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 83.03 | 41.37 |
| chat_preview | 0.970 | CAPS | 4/4 | 9110 | 0.417 | 13.14 | 11.99 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 1.51 | 1.02 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 52.35 | 36.99 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 19.79 | 1.27 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 103.04 | 35.79 |
| chat_preview | 0.985 | CAPS | 4/4 | 16139 | 0.739 | 11.58 | 10.87 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 1.48 | 0.90 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 77.83 | 40.06 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 19.71 | 1.67 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 99.77 | 36.24 |
| chat_preview | 0.995 | CAPS | 4/4 | 35350 | 1.620 | 23.36 | 22.52 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 3.60 | 1.62 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 63.16 | 59.63 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 75.42 | 2.02 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 5779 | 0.931 |
| chat_preview | 0.970 | 4 | 9062 | 0.981 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
