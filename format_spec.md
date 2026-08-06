# CAPS stream format v1

All integers are little-endian. A stream is self-contained and may be
validated without allocating a decoded image. Implementations must reject
dimensions above 16384, more than 4,000,000 chunks, invalid offsets, integer
overflow, unknown version, and checksum failures.

## Header (64 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `CAPS` magic |
| 4 | 2 | version = 1 |
| 6 | 2 | flags (currently zero) |
| 8 | 4 | source width |
| 12 | 4 | source height |
| 16 | 2 | number of detail pyramid levels |
| 18 | 2 | coefficient tile side (normally 32) |
| 20 | 1 | quality 1..100 |
| 21 | 1 | channel count = 3 |
| 22 | 2 | reserved |
| 24 | 4 | base LL width |
| 28 | 4 | base LL height |
| 32 | 4 | chunk count |
| 36 | 4 | directory byte count |
| 40 | 8 | payload start offset |
| 48 | 8 | reserved |
| 56 | 4 | FNV-1a checksum of header bytes 0..55 |
| 60 | 4 | reserved |

## Directory (40 bytes per chunk)

Chunks are ordered coarse-to-fine. Layer 0 is the base LL image; layer 1 is
the coarsest detail level; the largest layer is the finest detail level. A
chunk contains one channel and one band of a rectangular coefficient tile.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | progressive layer |
| 2 | 1 | band: 0=LL base, 1=horizontal, 2=vertical, 3=diagonal |
| 3 | 1 | channel: 0=Y, 1=Co, 2=Cg |
| 4 | 4 | coefficient x |
| 8 | 4 | coefficient y |
| 12 | 2 | tile width |
| 14 | 2 | tile height |
| 16 | 2 | scalar quantization step |
| 18 | 2 | entropy mode: 0=absolute varint, 1=delta varint, 2=significance-mask + bit-packed signed values |
| 20 | 8 | payload offset |
| 28 | 4 | payload bytes |
| 32 | 4 | coefficient count (tile width * height) |
| 36 | 4 | FNV-1a checksum of payload |

## Payload coding

Mode 0 scans each tile row-major. A variable-length unsigned integer gives the
number of zero coefficients before the next nonzero coefficient. If the tile
is not finished, a second unsigned integer gives `zigzag(q) + 1`, where `q`
is the signed quantized coefficient. A final zero run has no value token.
Mode 1 uses the same syntax but stores the delta from the previous nonzero
`q` in scan order. The dequantized coefficient is `q * step`.

Mode 2 starts with one byte `bits` (1..32), followed by a ceil(count/8)
significance bitset and tightly packed two's-complement signed values for the
set bits. The bit width is selected per tile; the decoder sign-extends and
multiplies by `step`. The encoder deterministically evaluates all three modes
and selects the smallest payload (ties prefer the lower mode number). This is
a deliberately small, deterministic entropy coder; tiles remain independently
decodable for spatial locality and progressive transport.

All coefficient planes are implicitly initialized to zero. An encoder may
therefore omit an all-zero tile entirely; absence of a directory entry for a
tile region means that its coefficients remain zero.

## Reconstruction

The reversible YCoCg-R channels are reconstructed from the base LL and the
selected detail layers by inverse 5/3 lifting. A decoder may stop after any
progressive layer; omitted details are zero. It may then bilinearly resample
the reconstructed prefix to any requested output resolution. The natural
rendering primitive is the compact-support 5/3 basis function associated with
each coefficient; the tile index is the spatial bin.
