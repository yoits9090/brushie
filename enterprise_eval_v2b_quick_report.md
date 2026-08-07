# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 112 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 39.44 | 21.03 |
| chat_preview | 0.970 | CAPS | 4/4 | 14533 | 0.666 | 4.22 | 4.10 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.51 | 0.44 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 12.39 | 1.09 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 38.73 | 21.57 |
| chat_preview | 0.985 | CAPS | 4/4 | 22732 | 1.042 | 4.59 | 4.51 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.50 | 0.45 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 12.70 | 1.13 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 40.20 | 21.94 |
| chat_preview | 0.995 | CAPS | 3/4 | 27079 | 1.241 | 4.98 | 5.15 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.53 | 0.62 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 8.54 | 1.47 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 12467 | 0.398 |
| chat_preview | 0.970 | 4 | 15921 | 0.474 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
