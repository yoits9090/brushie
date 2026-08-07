# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 152 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 69.28 | 21.85 |
| chat_preview | 0.970 | CAPS | 4/4 | 7039 | 0.323 | 3.87 | 3.77 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.87 | 0.49 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 19.26 | 1.15 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 39.86 | 22.86 |
| chat_preview | 0.985 | CAPS | 4/4 | 12000 | 0.550 | 3.91 | 3.98 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.93 | 0.54 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 19.61 | 1.21 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 41.28 | 21.77 |
| chat_preview | 0.995 | CAPS | 2/4 | 23326 | 1.069 | 4.24 | 4.14 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.54 | 0.61 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 8.91 | 1.38 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 11294 | 0.543 |
| chat_preview | 0.970 | 4 | 13290 | 0.622 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
