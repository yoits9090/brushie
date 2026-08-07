# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 188 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 78.30 | 35.43 |
| chat_preview | 0.970 | CAPS | 4/4 | 7042 | 0.323 | 5.74 | 6.00 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.70 | 0.50 |
| chat_preview | 0.970 | JPEG2000 | 4/4 | 10370 | 0.475 | 84.05 | 35.15 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 16.38 | 1.32 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 62.26 | 31.90 |
| chat_preview | 0.985 | CAPS | 4/4 | 12011 | 0.550 | 6.91 | 6.34 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.67 | 0.49 |
| chat_preview | 0.985 | JPEG2000 | 1/4 | 2850 | 0.131 | 64.62 | 33.98 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 15.22 | 1.35 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 62.54 | 32.40 |
| chat_preview | 0.995 | CAPS | 2/4 | 23340 | 1.069 | 6.02 | 7.39 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.98 | 0.83 |
| chat_preview | 0.995 | JPEG2000 | 1/4 | 2850 | 0.131 | 64.62 | 33.98 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 12.86 | 5.59 |

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
