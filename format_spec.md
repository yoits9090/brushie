# CAPS stream format v2

All integers are little-endian. A stream is self-contained and may be
validated without allocating a decoded image. Implementations must reject
dimensions above 16384, more than 4,000,000 chunks, invalid offsets, integer
overflow, unknown version, and checksum failures.

## Version history

* v1 (legacy): per-tile varint/bit-packed coefficient coding (entropy modes
  0..2). Still decodable by v2 decoders.
* v2 (current): whole-band context-adaptive arithmetic coding (mode 3),
  optional chroma 4:2:0, frequency-ordered quantization.

## Header (64 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `CAPS` magic |
| 4 | 2 | version = 2 |
| 6 | 2 | flags: bit 0 = chroma 4:2:0 subsampled; bits 1..15 reserved |
| 8 | 4 | source width |
| 12 | 4 | source height |
| 16 | 2 | number of detail pyramid levels (luma) |
| 18 | 2 | reserved (0) |
| 20 | 1 | quality 1..100 |
| 21 | 1 | channel count: 3 = RGB (Y, Co, Cg), 4 = RGBA (adds alpha) |
| 22 | 2 | reserved |
| 24 | 4 | base LL width (luma) |
| 28 | 4 | base LL height (luma) |
| 32 | 4 | chunk count |
| 36 | 4 | directory byte count |
| 40 | 8 | payload start offset |
| 48 | 4 | chroma base LL width (when flag bit 0 set) |
| 52 | 4 | chroma base LL height (when flag bit 0 set) |
| 56 | 4 | FNV-1a checksum of header bytes 0..55 |
| 60 | 4 | reserved |

## Directory (40 bytes per chunk)

In v2 a chunk is one whole band of one channel at one progressive layer;
there are no tile sub-chunks. Chunks are ordered coarse-to-fine: layer 0 is
the base LL image, layer 1 the coarsest detail level, and the largest layer
the finest detail level.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | progressive layer (0 = base LL) |
| 2 | 1 | band: 0=LL base, 1=horizontal, 2=vertical, 3=diagonal |
| 3 | 1 | channel: 0=Y, 1=Co, 2=Cg |
| 4 | 4 | coefficient x (always 0 in v2) |
| 8 | 4 | coefficient y (always 0 in v2) |
| 12 | 2 | band width |
| 14 | 2 | band height |
| 16 | 2 | scalar quantization step |
| 18 | 2 | entropy mode (v2 always 3) |
| 20 | 8 | payload offset |
| 28 | 4 | payload bytes |
| 32 | 4 | coefficient count (width * height) |
| 36 | 4 | FNV-1a checksum of payload |

## Payload coding: mode 3 (v2)

One payload per (layer, band, channel) band. Layout:

| Bytes | Field |
|---|---|
| 1 | initial Golomb-Rice parameter k0 (0..12) |
| rest | carryless binary range-coded stream (11-bit adaptive probabilities) |

Symbols per coefficient, in raster scan order:

1. **Significance** flag with an 8-state causal context: significant left,
   above, and above-left neighbours contribute 1, 2, and 4.
2. If significant: a **sign** bit with 4 sign contexts (left/above signs),
   then the magnitude minus one `m` coded as Golomb-Rice with parameter `k`:
   a unary quotient (per-position contexts, capped at 14) followed by `k`
   remainder bits (one adaptive context, LSB first).
3. `k` is re-adapted every 64 magnitudes to `floor(log2(mean m + 1))`,
   clamped to 0..12, identically on both sides.

The base LL band (layer 0, band 0) is median-predicted (JPEG-LS LOCO-I
predictor over left/above/above-left) before coding; the decoder applies the
same prediction when reconstructing.

All coefficient planes are implicitly initialized to zero, so an all-zero
band is omitted entirely. Quantization is midtread rounding to the nearest
`step`; the decoder reconstructs `q * step` exactly.

## Alpha (RGBA)

When channel count is 4, channel 3 is alpha. It is coded at full resolution
(no chroma-style subsampling) with the luma walk and luma quantization steps,
so hard transparency edges stay crisp. The stream otherwise behaves like an
RGB stream: chunks for channel 3 use luma band geometry, and the decoder
outputs four bytes per pixel.

## Chroma 4:2:0

When flag bit 0 is set, Co and Cg are box-downsampled 2x2 before the pyramid
and bilinearly upsampled after reconstruction. The chroma pyramid has its own
(possibly shorter) level stack and base dimensions stored at header offsets
48/52. A chunk's layer indexes into the luma stack for channel 0 and the
chroma stack for channels 1/2.

## Progressive and resolution decoding

A decoder may stop after any layer (chunks with a larger layer are ignored),
giving a lower-resolution approximation. It may request any output size; the
reconstructed pyramid prefix is bilinearly resampled.

## Reconstruction

The reversible YCoCg-R channels are reconstructed from the base LL and the
selected detail layers by inverse 5/3 lifting, then converted to RGB.
