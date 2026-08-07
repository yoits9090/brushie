# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 608 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 8/8 | 5290 | 0.242 | 60.86 | 27.08 |
| chat_preview | 0.970 | CAPS | 8/8 | 8853 | 0.406 | 4.30 | 4.65 |
| chat_preview | 0.970 | JPEG | 8/8 | 15485 | 0.710 | 0.88 | 0.85 |
| chat_preview | 0.970 | WebP | 8/8 | 11906 | 0.546 | 16.40 | 1.58 |
| chat_preview | 0.985 | AVIF | 8/8 | 9067 | 0.415 | 51.39 | 26.56 |
| chat_preview | 0.985 | CAPS | 8/8 | 12212 | 0.560 | 4.29 | 4.78 |
| chat_preview | 0.985 | JPEG | 8/8 | 16772 | 0.769 | 0.86 | 0.75 |
| chat_preview | 0.985 | WebP | 8/8 | 12586 | 0.577 | 16.49 | 1.59 |
| chat_preview | 0.995 | AVIF | 7/8 | 18564 | 0.851 | 55.61 | 28.24 |
| chat_preview | 0.995 | CAPS | 6/8 | 23525 | 1.078 | 5.42 | 5.67 |
| chat_preview | 0.995 | JPEG | 8/8 | 32803 | 1.503 | 0.74 | 0.80 |
| chat_preview | 0.995 | WebP | 8/8 | 19668 | 0.901 | 11.30 | 1.71 |
| expanded | 0.970 | AVIF | 8/8 | 11454 | 0.212 | 70.95 | 29.76 |
| expanded | 0.970 | CAPS | 8/8 | 17294 | 0.318 | 10.51 | 11.45 |
| expanded | 0.970 | JPEG | 8/8 | 35501 | 0.635 | 1.25 | 1.49 |
| expanded | 0.970 | WebP | 8/8 | 25108 | 0.475 | 24.01 | 3.17 |
| expanded | 0.985 | AVIF | 8/8 | 18340 | 0.352 | 71.70 | 30.87 |
| expanded | 0.985 | CAPS | 8/8 | 21979 | 0.405 | 11.90 | 11.61 |
| expanded | 0.985 | JPEG | 8/8 | 36463 | 0.655 | 1.24 | 1.48 |
| expanded | 0.985 | WebP | 8/8 | 25808 | 0.489 | 24.32 | 3.16 |
| expanded | 0.995 | AVIF | 8/8 | 35212 | 0.683 | 74.51 | 35.96 |
| expanded | 0.995 | CAPS | 7/8 | 39257 | 0.728 | 11.12 | 12.13 |
| expanded | 0.995 | JPEG | 8/8 | 56314 | 1.037 | 1.23 | 1.34 |
| expanded | 0.995 | WebP | 8/8 | 38718 | 0.748 | 25.03 | 3.47 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 8 | 11585 | 0.412 |
| chat_preview | 0.970 | 8 | 13420 | 0.480 |
| expanded | 0.950 | 8 | 18773 | 0.344 |
| expanded | 0.970 | 8 | 29384 | 0.525 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
