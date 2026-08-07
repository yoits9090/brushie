# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 608 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 8/8 | 5290 | 0.242 | 79.71 | 36.81 |
| chat_preview | 0.970 | CAPS | 8/8 | 8848 | 0.405 | 5.44 | 5.76 |
| chat_preview | 0.970 | JPEG | 8/8 | 15485 | 0.710 | 1.11 | 0.87 |
| chat_preview | 0.970 | WebP | 8/8 | 11906 | 0.546 | 23.44 | 2.16 |
| chat_preview | 0.985 | AVIF | 8/8 | 9067 | 0.415 | 65.46 | 37.69 |
| chat_preview | 0.985 | CAPS | 8/8 | 12203 | 0.559 | 5.71 | 5.98 |
| chat_preview | 0.985 | JPEG | 8/8 | 16772 | 0.769 | 1.15 | 0.89 |
| chat_preview | 0.985 | WebP | 8/8 | 12586 | 0.577 | 23.70 | 2.18 |
| chat_preview | 0.995 | AVIF | 7/8 | 18564 | 0.851 | 65.27 | 37.96 |
| chat_preview | 0.995 | CAPS | 6/8 | 23508 | 1.077 | 6.51 | 6.68 |
| chat_preview | 0.995 | JPEG | 8/8 | 32803 | 1.503 | 0.85 | 0.98 |
| chat_preview | 0.995 | WebP | 8/8 | 19668 | 0.901 | 16.42 | 2.44 |
| expanded | 0.970 | AVIF | 8/8 | 11454 | 0.212 | 86.54 | 40.60 |
| expanded | 0.970 | CAPS | 8/8 | 17286 | 0.318 | 11.68 | 13.23 |
| expanded | 0.970 | JPEG | 8/8 | 35501 | 0.635 | 1.90 | 1.95 |
| expanded | 0.970 | WebP | 8/8 | 25108 | 0.475 | 37.84 | 4.67 |
| expanded | 0.985 | AVIF | 8/8 | 18340 | 0.352 | 87.04 | 41.22 |
| expanded | 0.985 | CAPS | 8/8 | 21968 | 0.405 | 11.97 | 13.69 |
| expanded | 0.985 | JPEG | 8/8 | 36463 | 0.655 | 1.92 | 1.95 |
| expanded | 0.985 | WebP | 8/8 | 25808 | 0.489 | 38.14 | 4.72 |
| expanded | 0.995 | AVIF | 8/8 | 35212 | 0.683 | 86.82 | 43.15 |
| expanded | 0.995 | CAPS | 7/8 | 39231 | 0.728 | 13.76 | 15.17 |
| expanded | 0.995 | JPEG | 8/8 | 56314 | 1.037 | 1.86 | 2.13 |
| expanded | 0.995 | WebP | 8/8 | 38718 | 0.748 | 40.91 | 5.46 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 8 | 11559 | 0.411 |
| chat_preview | 0.970 | 8 | 13392 | 0.480 |
| expanded | 0.950 | 8 | 18743 | 0.344 |
| expanded | 0.970 | 8 | 29340 | 0.524 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
