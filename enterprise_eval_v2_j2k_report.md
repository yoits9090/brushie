# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 176 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 100.15 | 41.27 |
| chat_preview | 0.970 | CAPS | 4/4 | 7042 | 0.323 | 6.91 | 8.01 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 1.00 | 1.03 |
| chat_preview | 0.970 | JPEG2000 | 1/4 | 2903 | 0.133 | 59.24 | 28.14 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 27.40 | 1.57 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 71.20 | 29.03 |
| chat_preview | 0.985 | CAPS | 4/4 | 12011 | 0.550 | 8.17 | 6.94 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 1.00 | 1.15 |
| chat_preview | 0.985 | JPEG2000 | 1/4 | 2903 | 0.133 | 59.24 | 28.14 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 31.83 | 1.65 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 75.67 | 33.78 |
| chat_preview | 0.995 | CAPS | 2/4 | 23340 | 1.069 | 6.69 | 6.56 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 1.32 | 1.19 |
| chat_preview | 0.995 | JPEG2000 | 1/4 | 2903 | 0.133 | 59.24 | 28.14 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 17.20 | 2.27 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 11316 | 0.543 |
| chat_preview | 0.970 | 4 | 13312 | 0.622 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
