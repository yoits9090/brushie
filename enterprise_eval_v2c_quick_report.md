# CAPS enterprise evaluation

This is a directional engineering evaluation, not a customer-facing
quality claim. MS-SSIM values use the repository's deterministic proxy;
LPIPS and blinded human preference testing are still required.

Evaluated 112 encoded candidates.

## Equal-quality aggregate

| Profile | MS-SSIM gate | Codec | Coverage | Mean bytes | Mean bpp | Encode ms | Decode ms |
|---|---:|---|---:|---:|---:|---:|---:|
| chat_preview | 0.970 | AVIF | 4/4 | 4094 | 0.188 | 43.51 | 18.77 |
| chat_preview | 0.970 | CAPS | 4/4 | 14796 | 0.678 | 3.77 | 3.73 |
| chat_preview | 0.970 | JPEG | 4/4 | 12396 | 0.568 | 0.70 | 0.40 |
| chat_preview | 0.970 | WebP | 4/4 | 8738 | 0.400 | 12.12 | 0.87 |
| chat_preview | 0.985 | AVIF | 4/4 | 8568 | 0.393 | 34.05 | 18.57 |
| chat_preview | 0.985 | CAPS | 4/4 | 22866 | 1.048 | 4.06 | 3.94 |
| chat_preview | 0.985 | JPEG | 4/4 | 14971 | 0.686 | 0.78 | 0.41 |
| chat_preview | 0.985 | WebP | 4/4 | 10098 | 0.463 | 12.34 | 0.90 |
| chat_preview | 0.995 | AVIF | 3/4 | 18560 | 0.850 | 34.81 | 19.10 |
| chat_preview | 0.995 | CAPS | 3/4 | 27284 | 1.250 | 4.16 | 4.10 |
| chat_preview | 0.995 | JPEG | 4/4 | 36428 | 1.669 | 0.42 | 0.49 |
| chat_preview | 0.995 | WebP | 4/4 | 19704 | 0.903 | 7.76 | 1.19 |

## CAPS progressive preview

| Profile | Preview gate | Samples | Mean prefix bytes | Mean fraction of full |
|---|---:|---:|---:|---:|
| chat_preview | 0.950 | 4 | 12546 | 0.399 |
| chat_preview | 0.970 | 4 | 15974 | 0.474 |

## Decision rule

Do not claim an infrastructure-cost advantage until CAPS beats the
smallest standard codec at full coverage and the same quality gate.
Its progressive path should be evaluated separately on bytes and time
to first useful preview.
