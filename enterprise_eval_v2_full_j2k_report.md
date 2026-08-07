# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 704 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 8/8 | 5290 | 0.242 | 90.46 | 31.31 |
| chat_preview | 0.970 | CAPS | 8/8 | 8853 | 0.406 | 5.95 | 6.69 |
| chat_preview | 0.970 | JPEG | 8/8 | 15485 | 0.710 | 1.08 | 0.94 |
| chat_preview | 0.970 | JPEG2000 | 4/8 | 4559 | 0.209 | 80.17 | 32.31 |
| chat_preview | 0.970 | WebP | 8/8 | 11906 | 0.546 | 26.61 | 3.52 |
| chat_preview | 0.985 | AVIF | 8/8 | 9067 | 0.415 | 74.31 | 32.71 |
| chat_preview | 0.985 | CAPS | 8/8 | 12212 | 0.560 | 6.94 | 8.33 |
| chat_preview | 0.985 | JPEG | 8/8 | 16772 | 0.769 | 1.11 | 0.95 |
| chat_preview | 0.985 | JPEG2000 | 4/8 | 6678 | 0.306 | 84.71 | 30.76 |
| chat_preview | 0.985 | WebP | 8/8 | 12586 | 0.577 | 25.87 | 3.34 |
| chat_preview | 0.995 | AVIF | 7/8 | 18564 | 0.851 | 78.00 | 32.99 |
| chat_preview | 0.995 | CAPS | 6/8 | 23525 | 1.078 | 6.78 | 7.16 |
| chat_preview | 0.995 | JPEG | 8/8 | 32803 | 1.503 | 1.29 | 0.95 |
| chat_preview | 0.995 | JPEG2000 | 1/8 | 2903 | 0.133 | 70.29 | 32.18 |
| chat_preview | 0.995 | WebP | 8/8 | 19668 | 0.901 | 17.43 | 3.97 |
| expanded | 0.970 | AVIF | 8/8 | 11454 | 0.212 | 101.31 | 33.09 |
| expanded | 0.970 | CAPS | 8/8 | 17294 | 0.318 | 13.53 | 13.42 |
| expanded | 0.970 | JPEG | 8/8 | 35501 | 0.635 | 1.73 | 1.46 |
| expanded | 0.970 | JPEG2000 | 6/8 | 16066 | 0.310 | 182.57 | 45.64 |
| expanded | 0.970 | WebP | 8/8 | 25108 | 0.475 | 32.08 | 3.48 |
| expanded | 0.985 | AVIF | 8/8 | 18340 | 0.352 | 83.43 | 35.19 |
| expanded | 0.985 | CAPS | 8/8 | 21979 | 0.405 | 11.35 | 12.58 |
| expanded | 0.985 | JPEG | 8/8 | 36463 | 0.655 | 1.63 | 1.56 |
| expanded | 0.985 | JPEG2000 | 4/8 | 12747 | 0.234 | 181.11 | 48.30 |
| expanded | 0.985 | WebP | 8/8 | 25808 | 0.489 | 34.92 | 3.51 |
| expanded | 0.995 | AVIF | 8/8 | 35212 | 0.683 | 86.84 | 35.62 |
| expanded | 0.995 | CAPS | 7/8 | 39257 | 0.728 | 13.43 | 15.21 |
| expanded | 0.995 | JPEG | 8/8 | 56314 | 1.037 | 1.51 | 3.06 |
| expanded | 0.995 | JPEG2000 | 1/8 | 8378 | 0.070 | 126.30 | 54.57 |
| expanded | 0.995 | WebP | 8/8 | 38718 | 0.748 | 30.71 | 3.67 |

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
