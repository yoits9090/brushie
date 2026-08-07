# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 116 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 46.46 | 20.73 |
| chat_preview | 0.970 | CAPS | 4/4 | 7459 | 0.342 | 3.65 | 4.63 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.80 | 0.45 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 12.89 | 0.98 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 39.50 | 20.76 |
| chat_preview | 0.985 | CAPS | 4/4 | 14873 | 0.681 | 3.91 | 3.92 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.80 | 0.47 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 13.04 | 1.05 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 40.67 | 21.25 |
| chat_preview | 0.995 | CAPS | 3/4 | 20789 | 0.953 | 4.25 | 4.93 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.57 | 0.70 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 8.55 | 1.42 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 11684 | 0.538 |
| chat_preview | 0.970 | 4 | 13772 | 0.617 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
