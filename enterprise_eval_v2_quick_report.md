# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 148 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 117.08 | 18.70 |
| chat_preview | 0.970 | CAPS | 4/4 | 43278 | 1.983 | 4.96 | 5.00 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.48 | 0.45 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 21.88 | 0.94 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 33.60 | 19.18 |
| chat_preview | 0.985 | CAPS | 4/4 | 43732 | 2.004 | 4.95 | 5.00 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.46 | 0.43 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 22.06 | 0.97 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 35.14 | 20.21 |
| chat_preview | 0.995 | CAPS | 3/4 | 41830 | 1.917 | 4.91 | 4.80 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.43 | 0.50 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 8.56 | 1.22 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 16928 | 0.411 |
| chat_preview | 0.970 | 3 | 29538 | 0.622 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
