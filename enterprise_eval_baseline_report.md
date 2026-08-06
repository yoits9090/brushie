# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 148 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 104.55 | 38.77 |
| chat_preview | 0.970 | CAPS | 4/4 | 46251 | 2.119 | 9.93 | 6.49 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 1.50 | 0.91 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 31.96 | 1.96 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 70.78 | 39.28 |
| chat_preview | 0.985 | CAPS | 4/4 | 63305 | 2.901 | 10.63 | 6.87 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 1.48 | 0.88 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 32.38 | 2.03 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 72.44 | 39.34 |
| chat_preview | 0.995 | CAPS | 4/4 | 113430 | 5.197 | 12.67 | 7.61 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.89 | 1.00 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 15.48 | 2.55 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 29322 | 0.344 |
| chat_preview | 0.970 | 4 | 38633 | 0.405 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
