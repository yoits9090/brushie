# CAPS byte accounting

`scripts/byte_audit.py` parses the complete stream, including the header,
directory, payloads, and per-tile checksums. It reports empirical entropy of
the nonzero quantized symbols and a lower bound for that symbol stream in
`entropy_audit.csv`.

The current v1 format has fixed 64-byte header and 40-byte directory entries.
The directory is therefore 40 bytes per tile, even when a tile contains only a
trailing zero run in historical streams. Current encoders omit all-zero tiles,
which are already represented by the decoder's zero-initialized coefficient
planes. Adaptive 64-pixel tiles additionally reduce the number of nonempty
directory entries without changing reconstruction.
The audit also separates coefficient payload from directory bytes and groups it
by progressive layer, subband, and Y/Co/Cg channel. Any target-size result must
include every one of these bytes.

Measured final 4096² Kodak point (`q=82`, adaptive tile, 8 threads):

- header: 64 bytes;
- directory: 491,520 bytes;
- coefficient payload: 1,700,729 bytes;
- total: 2,192,313 bytes (1.045 bpp);
- 50,331,648 coefficients, 1,500,592 nonzero;
- 11,127 mode-0 tiles, 0 mode-1 tiles, 1,161 mode-2 tiles.

Mode 2 stores one significance bit per coefficient plus a per-tile signed bit
width. The encoder evaluates modes in one quantization pass and one emission
pass, so mode competition remains within the two post-pyramid full-resolution
passes. The old delta mode is retained for stream compatibility but is not
selected on the frozen Kodak point.

Baseline audit labels:

- IMPLEMENTED: complete stream accounting and empirical symbol entropy;
- MEASURED: values in `entropy_audit.csv` and `benchmark_results.csv`;
- PROPOSED: compact directory and hybrid tile modes;
- NOVELTY UNRESOLVED: all proposed modes require comparison with established
  transform/image codecs before a novelty claim.
