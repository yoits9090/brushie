# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 148 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 64.80 | 35.17 |
| chat_preview | 0.970 | CAPS | 4/4 | 44974 | 2.061 | 9.70 | 6.42 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.79 | 0.75 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 18.40 | 1.62 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 85.02 | 37.41 |
| chat_preview | 0.985 | CAPS | 4/4 | 61966 | 2.839 | 10.01 | 6.51 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.75 | 0.74 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 18.83 | 1.68 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 66.76 | 36.54 |
| chat_preview | 0.995 | CAPS | 4/4 | 112614 | 5.160 | 11.98 | 7.21 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.78 | 0.92 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 14.96 | 2.31 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 27974 | 0.325 |
| chat_preview | 0.970 | 4 | 37284 | 0.386 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
