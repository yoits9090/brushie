# Brushie enterprise readiness

## Proposed product

Brushie is best evaluated as an on-premise codec SDK for platforms that own
their media pipeline and clients. The enterprise claim to test is:

> Reduce delivered image bytes at the same human-rated quality without
> increasing total infrastructure cost or user-visible latency.

This is an SDK/source-license product, not initially a hosted SaaS. A
Discord-scale customer would need to run the encoder inside its own media
proxy and deploy trusted decoders across native, desktop, and web clients.

## Current equal-quality result (v2 codec)

`scripts/enterprise_eval.py` sweeps 18 quality points per sample across six
photographs, two deterministic chat/UI images, a 512-pixel preview profile,
and a 1536-pixel expanded-image profile. It selects the smallest candidate
meeting each MS-SSIM-proxy gate.

Full profile (8 samples: 6 photographs + chat + meme; mean bytes):

| Profile | Gate | CAPS v2 | WebP | AVIF | JPEG |
|---|---|---:|---:|---:|---:|
| chat 512px | .970 | 8,848 | 11,906 | 5,290 | 15,485 |
| chat 512px | .985 | 12,203 | 12,586 | 9,067 | 16,772 |
| chat 512px | .995 | 23,508 | 19,668 | 18,564 | 32,803 |
| expanded 1536px | .970 | 17,286 | 25,108 | 11,454 | 35,501 |
| expanded 1536px | .985 | 21,969 | 25,808 | 18,340 | 36,463 |
| expanded 1536px | .995 | 39,231 | 38,719 | 35,212 | 56,314 |

The v1 baseline at the same profile was 53,642 / 62,138 / 106,385 bytes
(chat) and 109,348 / 124,861 / 172,973 (expanded), so the v2 codec is 4.4-6.3x
smaller at equal proxy quality. CAPS now beats JPEG at every gate and beats
WebP at the .970 and .985 gates on both profiles; at .995 it is essentially
tied with WebP (1.01-1.20x) and 1.1-1.7x behind AVIF. On individual samples
CAPS already beats AVIF (e.g. photo_01 at .995: 45,121 vs 47,581 bytes).

CAPS encodes a 512px preview in roughly 5.4 ms (8 threads) and decodes in
~5.8 ms; a 1536px expanded image takes ~12-14 ms each way. The comparable
AVIF path (ffmpeg/libsvtav1, including process and file I/O) takes ~65-87 ms
to encode, so CAPS is roughly an order of magnitude cheaper to encode at a
competitive-or-better byte count than JPEG/WebP on the low and middle gates.
No production CPU-cost claim should be made from these timings (the harness
scopes are not equivalent), but the direction is strong.

## What changed in v2

* **Context-adaptive arithmetic coding** replaced per-tile varint/bitpack
  coding: 8-state significance contexts, 4 sign contexts, Golomb-Rice
  magnitudes with adaptive k, and a median-predicted base LL band.
* **Whole-band chunks**: one directory entry per (layer, band, channel)
  instead of per 32-64px tile; the directory is now a rounding error.
* **Frequency-ordered quantization**: coarse detail is quantized hardest
  (steps grow 2^(1.25*coarseness), saturating after 3 levels), the base LL is
  preserved with a finer step, and the diagonal band is weighted 1.5x.
* **Chroma 4:2:0** at lossy operating points (box downsample, bilinear
  upsample), keeping 4:4:4 for lossless.
* **Midtread quantization** with plain q*step reconstruction; a dead-zone
  variant was measurably worse on the frozen sweep.

## Remaining gaps

The biggest remaining byte gap is photos at the .970 gate (1.7x behind AVIF
on the quick profile). The codec still uses the reversible 5/3 wavelet and
scalar quantization; AV1-class tools (directional intra prediction,
rate-distortion-optimized allocation, a 9/7-class lossy transform) are the
obvious next levers, with expected gains of 10-30% on photos.

## Go/no-go gate

Do not approach a Discord-scale buyer with an infrastructure-savings claim
until all of the following are true on a representative private corpus:

1. At least 20% fewer bytes than the buyer's current WebP/AVIF pipeline at
   the same blinded human preference level.
2. No increase in combined encode, decode, and CDN cost that erases the byte
   savings.
3. A safe incremental rollout with standard-format fallback.
4. Production support for alpha, color profiles, metadata, malformed-input
   hardening, fuzzing, Linux x86/ARM, mobile, and WebAssembly.
5. A clean IP and dependency record plus explicit commercial licensing.

The v2 codec is now plausibly competitive on bytes for UI/chat content and
low-quality photo delivery, and is materially cheaper to encode than AVIF.
The remaining work is perceptual validation (LPIPS, blinded human tests),
the alpha/color-profile/metadata surface, security hardening, and the
transform/rate-allocation improvements above.

## Reproduction

```sh
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic \
  -Iinclude src/codec.cpp src/main.cpp -o build/brushie -pthread
clang++ -std=c++17 -O3 -ffast-math -Wall -Wextra -Wpedantic \
  -Iinclude src/codec.cpp tests/test_codec.cpp -o build/test_codec -pthread
./build/test_codec
python3 scripts/enterprise_eval.py --quick --output-prefix enterprise_eval_v2
```

The primary outputs are the `enterprise_eval_v2_*` CSV and Markdown files.
