# Brushie enterprise readiness (harness v3 reset)

## Status

The historical readiness tables were invalidated. The old harness used one
global mean/variance/covariance value per RGB channel and called it MS-SSIM,
searched CAPS much more densely than standards, underpowered WebP/JPEG/AVIF,
compared survivor means at partial coverage, and mixed incompatible timing
scopes. See [docs/harness_v3.md](docs/harness_v3.md).

Corrected clean exhaustive quick profile (2 Kodak photos + chat + meme,
2,923 encodes, local-window multiscale SSIM, every legal quality, strong
format modes, all 4/4 coverage):

| Gate | AVIF | WebP | optimized JPEG | JPEG 2000 | CAPS |
|---:|---:|---:|---:|---:|---:|
| .970 | 6,466 | 7,658 | 10,140 | 10,046 | 11,346 |
| .985 | 12,948 | 13,901 | 16,328 | 19,084 | 19,351 |
| .995 | 31,810 | 65,498 | 32,427 | 44,377 | 77,869 |

Brushie **does not currently beat any listed codec at the corrected gates**.
At .970 it is close to strong JPEG/JPEG 2000 but 48% larger than WebP and 75%
larger than AVIF. At .985 it is 19% larger than JPEG and 39% larger than
WebP. At .995 its full-coverage rate is 2.4x AVIF/JPEG because one hard photo
falls back to lossless. These numbers (manifest SHA `0561782`) are the current
north star.

## What remains genuinely good

- Deterministic CPU-only codec and decoder; no per-image ML/gradient fitting.
- Whole-band context-adaptive arithmetic coding; progressive layer order.
- RGB/RGBA, full-resolution alpha, optional PNG CLI I/O.
- Actual physical truncated-prefix decoding after the harness-v3 decoder fix.
- 8,000+ mutation-fuzz cases with no crash/hang before the latest prefix fix;
  sanitizer/fuzz rerun remains required.
- Peak 4096^2 RSS was reduced from ~527 MB historical to ~413 MB encode /
  ~420 MB decode on the local synthetic stress image.

These are engineering assets, not evidence of format superiority.

## Commercial gate

Do not make a buyer-facing savings claim until all of the following hold on a
private corpus:

1. Full-coverage paired results beat the buyer's WebP/AVIF/JPEG-XL pipeline at
   blinded human preference, not a single proxy metric.
2. SSIMULACRA2 or Butteraugli and local-window MS-SSIM agree directionally.
3. End-to-end CPU, peak RSS, and power are measured from identical boundaries
   with warmups/repeats and a fixed thread/resource budget.
4. Alpha, ICC/color, metadata, malformed input, fuzz/sanitizers, Linux
   x86/ARM, mobile, and WebAssembly are production-ready.
5. Standard-format fallback and incremental rollout are documented.

## Next engineering target

The corrected deficit is structural, not a directory or varint problem:

- .970/.985: prediction/transform efficiency and smooth/text handling.
- .995: high-quality rate allocation; one hard photo falls back to lossless.
- Text/UI: AV1-style directional/planar prediction remains the largest gap.

The next codec branch should prototype a **planar/directional base+coarse mode**
for flat gradients and text/UI, compete it per image/band against the wavelet
path, and retain it only if it lowers bytes at the corrected gate. Follow with
coder-integrated RDO (actual context-rate estimates), not another static
dead-zone heuristic.

## Reproduction

```sh
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic \
  -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic \
  -Iinclude src/codec.cpp tests/test_codec.cpp -o build/test_codec -pthread
./build/test_codec
python3 tests/test_quality_metrics.py
python3 tests/test_enterprise_eval_helpers.py
python3 scripts/enterprise_eval.py --quick --output-prefix harness_v3_exhaustive_quick
```

Every report emits a manifest with the metric ID, git SHA/dirty state,
versions, exact command, source/evaluated dimensions, and codec configs.
Partial-coverage aggregate means are blank by design.
