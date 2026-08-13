# Real-metric landscape re-gate (harness=real-metrics)

- images: 163 (broad corpus, 512px)
- source rows: `benchmarks/real/v1/rows.csv`
- metrics: SSIMULACRA2 (v2.1 standalone) / Butteraugli 3-norm (libjxl 0.13)
- manifest: `benchmarks/real/v1/manifest.json`
- selection: per image, per gate, smallest bytes clearing the gate

## Codecs measured

AVIF, CAPS, JPEG, JPEG XL, JPEG2000, WebP

## SSIMULACRA2 (higher is better)

## ssimulacra2 gate 60.0 (>= 60.0)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 159/163 PARTIAL | 18,374 | 97,368 (0835x2) | 61.987 |
| CAPS | 163/163 | 23,151 | 75,069 (4.2.03) | 60.557 |
| JPEG | 163/163 | 22,101 | 67,245 (0897x2) | 60.741 |
| JPEG XL | 163/163 | 18,607 | 52,687 (0897x2) | 63.310 |
| JPEG2000 | 163/163 | 24,386 | 88,326 (0835x2) | 61.587 |
| WebP | 163/163 | 18,534 | 55,300 (4.2.03) | 60.699 |

## ssimulacra2 gate 75.0 (>= 75.0)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 157/163 PARTIAL | 31,124 | 88,644 (4.2.03) | 76.038 |
| CAPS | 163/163 | 45,253 | 226,315 (4.2.03) | 75.860 |
| JPEG | 163/163 | 39,528 | 141,307 (0897x2) | 75.582 |
| JPEG XL | 163/163 | 32,003 | 83,403 (4.2.03) | 77.247 |
| JPEG2000 | 163/163 | 42,606 | 144,724 (0835x2) | 76.350 |
| WebP | 161/163 PARTIAL | 32,966 | 101,322 (4.2.03) | 75.829 |

## ssimulacra2 gate 85.0 (>= 85.0)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 140/163 PARTIAL | 56,381 | 245,749 (4.2.03) | 85.809 |
| CAPS | 163/163 | 89,271 | 307,011 (4.2.03) | 86.122 |
| JPEG | 144/163 PARTIAL | 68,546 | 191,256 (house) | 85.771 |
| JPEG XL | 163/163 | 52,408 | 140,338 (4.2.03) | 86.387 |
| JPEG2000 | 163/163 | 85,917 | 282,515 (4.2.03) | 86.315 |
| WebP | 129/163 PARTIAL | 56,558 | 121,316 (0822x2) | 85.725 |

## ssimulacra2 gate 90.0 (>= 90.0)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 79/163 PARTIAL | 82,263 | 153,350 (0872x2) | 90.739 |
| CAPS | 163/163 | 182,609 | 525,365 (4.2.06) | 95.288 |
| JPEG | 89/163 PARTIAL | 93,072 | 176,236 (0878x2) | 90.831 |
| JPEG XL | 163/163 | 75,820 | 193,699 (4.2.03) | 91.048 |
| JPEG2000 | 159/163 PARTIAL | 162,074 | 373,149 (4.2.03) | 91.199 |
| WebP | 57/163 PARTIAL | 72,159 | 111,542 (18) | 90.528 |

## Butteraugli 3-norm (lower is better)

## butteraugli gate 1.3 (<= 1.3)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 133/163 PARTIAL | 30,270 | 88,644 (4.2.03) | 1.247 |
| CAPS | 163/163 | 45,958 | 226,315 (4.2.03) | 1.220 |
| JPEG | 150/163 PARTIAL | 36,768 | 119,484 (0894x2) | 1.270 |
| JPEG XL | 163/163 | 28,173 | 70,761 (4.2.03) | 1.176 |
| JPEG2000 | 163/163 | 44,428 | 137,518 (0835x2) | 1.246 |
| WebP | 149/163 PARTIAL | 28,442 | 93,104 (4.2.03) | 1.268 |

## butteraugli gate 0.9 (<= 0.9)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 98/163 PARTIAL | 45,603 | 182,088 (0870x2) | 0.863 |
| CAPS | 163/163 | 81,162 | 256,710 (4.2.03) | 0.814 |
| JPEG | 135/163 PARTIAL | 52,802 | 194,650 (0891x2) | 0.862 |
| JPEG XL | 163/163 | 40,193 | 100,813 (4.2.03) | 0.807 |
| JPEG2000 | 163/163 | 78,116 | 264,251 (4.2.03) | 0.831 |
| WebP | 132/163 PARTIAL | 41,824 | 91,052 (0897x2) | 0.863 |

## butteraugli gate 0.6 (<= 0.6)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 71/163 PARTIAL | 69,838 | 153,079 (0821x2) | 0.564 |
| CAPS | 163/163 | 164,125 | 484,579 (4.2.07) | 0.322 |
| JPEG | 111/163 PARTIAL | 77,956 | 188,457 (0821x2) | 0.552 |
| JPEG XL | 163/163 | 56,120 | 148,693 (4.2.06) | 0.539 |
| JPEG2000 | 160/163 PARTIAL | 145,618 | 338,790 (5.3.02) | 0.535 |
| WebP | 100/163 PARTIAL | 63,837 | 106,306 (0832x2) | 0.568 |

## butteraugli gate 0.4 (<= 0.4)

| Codec | Coverage | Mean bytes (aligned on mean) | Worst-case bytes (aligned on worst image) | Mean metric at selection |
|---|---:|---:|---:|---:|
| AVIF | 32/163 PARTIAL | 89,058 | 148,010 (0883x2) | 0.343 |
| CAPS | 163/163 | 223,644 | 591,293 (4.2.03) | 0.051 |
| JPEG | 73/163 PARTIAL | 100,246 | 160,796 (0858x2) | 0.351 |
| JPEG XL | 163/163 | 79,705 | 231,752 (4.2.03) | 0.348 |
| JPEG2000 | 76/163 PARTIAL | 196,293 | 432,457 (4.2.03) | 0.355 |
| WebP | 45/163 PARTIAL | 90,995 | 151,968 (0822x2) | 0.372 |

## Proxy-gate sanity check (pipeline validation)

| Codec | gate .970 | gate .985 | gate .995 |
|---|---:|---:|---:|
| AVIF | 9,397 | 18,640 | 43,613 |
| CAPS | 11,810 | 22,219 | 57,864 |
| JPEG | 15,091 | 26,912 | 53,563 |
| JPEG XL | 11,758 | 22,827 | 54,410 |
| JPEG2000 | 13,347 | 24,813 | 58,607 |
| WebP | 11,145 | 20,253 | 43,168 |
