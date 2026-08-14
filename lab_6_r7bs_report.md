# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. The primary gate is local-window multiscale SSIM;
LPIPS and blinded human preference testing are still required.

Metric: `brushie-box11-ms-ssim-v1`.
Evaluated 3323 encoded candidates.

## Equal-quality aggregate

| Profile | Windowed MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode wall ms | Decode wall ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 6466 | 0.296 | 122.01 | 51.22 |
| chat_preview | 0.970 | CAPS | 4/4 | 8397 | 0.385 | 20.12 | 15.90 |
| chat_preview | 0.970 | JPEG | 4/4 | 10140 | 0.465 | 2.52 | 1.05 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10046 | 0.460 | 70.64 | 51.53 |
| chat_preview | 0.970 | WebP | 4/4 | 7658 | 0.351 | 28.83 | 3.85 |
| chat_preview | 0.985 | AVIF | 4/4 | 12948 | 0.593 | 148.78 | 46.10 |
| chat_preview | 0.985 | CAPS | 4/4 | 15257 | 0.699 | 27.70 | 21.45 |
| chat_preview | 0.985 | JPEG | 4/4 | 16328 | 0.748 | 1.98 | 1.15 |
| chat_preview | 0.985 | JPEG2000 | 4/4 | 19084 | 0.874 | 82.05 | 47.12 |
| chat_preview | 0.985 | WebP | 4/4 | 13901 | 0.637 | 33.62 | 2.40 |
| chat_preview | 0.995 | AVIF | 4/4 | 31810 | 1.458 | 139.11 | 73.54 |
| chat_preview | 0.995 | CAPS | 4/4 | 33950 | 1.556 | 48.89 | 29.49 |
| chat_preview | 0.995 | JPEG | 4/4 | 32427 | 1.486 | 8.16 | 2.93 |
| chat_preview | 0.995 | JPEG2000 | 4/4 | 44377 | 2.033 | 80.68 | 55.57 |
| chat_preview | 0.995 | WebP | 4/4 | 65498 | 3.001 | 248.37 | 4.37 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 5228 | 0.934 |
| chat_preview | 0.970 | 4 | 8356 | 0.979 |

## Decision rule

Rows without full coverage are diagnostic only and cannot be compared
to full-coverage codecs. Do not claim an infrastructure-cost advantage
until CAPS beats the smallest standard codec at full coverage and the
same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
