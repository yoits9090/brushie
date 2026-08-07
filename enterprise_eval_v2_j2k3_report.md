# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 196 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 70.01 | 33.61 |
| chat_preview | 0.970 | CAPS | 4/4 | 7042 | 0.323 | 7.63 | 9.96 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 1.02 | 0.64 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10370 | 0.475 | 92.93 | 40.99 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 24.82 | 1.39 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 70.79 | 34.92 |
| chat_preview | 0.985 | CAPS | 4/4 | 12011 | 0.550 | 6.61 | 7.94 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 1.05 | 0.65 |
| chat_preview | 0.985 | JPEG2000 | 2/4 | 5550 | 0.254 | 61.08 | 32.77 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 27.38 | 1.45 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 78.89 | 35.21 |
| chat_preview | 0.995 | CAPS | 2/4 | 23340 | 1.069 | 7.92 | 8.48 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.76 | 0.85 |
| chat_preview | 0.995 | JPEG2000 | 1/4 | 2850 | 0.131 | 65.31 | 31.28 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 14.40 | 1.85 |

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
