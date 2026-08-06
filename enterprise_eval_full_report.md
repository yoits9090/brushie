# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 592 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 8/8 | 5290 | 0.242 | 67.04 | 37.06 |
| chat_preview | 0.970 | CAPS | 8/8 | 53642 | 2.458 | 9.48 | 6.27 |
| chat_preview | 0.970 | JPEG | 8/8 | 15485 | 0.710 | 0.88 | 0.86 |
| chat_preview | 0.970 | WebP | 8/8 | 11906 | 0.546 | 17.51 | 2.00 |
| chat_preview | 0.985 | AVIF | 8/8 | 9067 | 0.415 | 67.02 | 38.84 |
| chat_preview | 0.985 | CAPS | 8/8 | 62138 | 2.847 | 9.83 | 6.46 |
| chat_preview | 0.985 | JPEG | 8/8 | 16772 | 0.769 | 0.87 | 0.87 |
| chat_preview | 0.985 | WebP | 8/8 | 12586 | 0.577 | 17.74 | 2.04 |
| chat_preview | 0.995 | AVIF | 7/8 | 18564 | 0.851 | 67.23 | 38.48 |
| chat_preview | 0.995 | CAPS | 8/8 | 106385 | 4.875 | 11.60 | 7.14 |
| chat_preview | 0.995 | JPEG | 8/8 | 32803 | 1.503 | 0.88 | 0.97 |
| chat_preview | 0.995 | WebP | 8/8 | 19668 | 0.901 | 16.18 | 2.47 |
| expanded | 0.970 | AVIF | 8/8 | 11454 | 0.212 | 83.67 | 40.65 |
| expanded | 0.970 | CAPS | 8/8 | 109348 | 2.025 | 20.97 | 12.72 |
| expanded | 0.970 | JPEG | 8/8 | 35501 | 0.635 | 1.81 | 2.11 |
| expanded | 0.970 | WebP | 8/8 | 25108 | 0.475 | 36.57 | 4.75 |
| expanded | 0.985 | AVIF | 8/8 | 18340 | 0.352 | 84.51 | 41.53 |
| expanded | 0.985 | CAPS | 8/8 | 124861 | 2.307 | 21.65 | 12.99 |
| expanded | 0.985 | JPEG | 8/8 | 36463 | 0.655 | 1.80 | 2.11 |
| expanded | 0.985 | WebP | 8/8 | 25808 | 0.489 | 36.77 | 4.79 |
| expanded | 0.995 | AVIF | 8/8 | 35212 | 0.683 | 87.44 | 44.37 |
| expanded | 0.995 | CAPS | 8/8 | 172973 | 3.269 | 22.84 | 13.69 |
| expanded | 0.995 | JPEG | 8/8 | 56314 | 1.037 | 1.86 | 2.19 |
| expanded | 0.995 | WebP | 8/8 | 38718 | 0.748 | 39.74 | 5.61 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 8 | 28390 | 0.244 |
| chat_preview | 0.970 | 8 | 34032 | 0.281 |
| expanded | 0.950 | 8 | 46805 | 0.218 |
| expanded | 0.970 | 8 | 82185 | 0.371 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
