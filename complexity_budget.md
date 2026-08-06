# CAPS acceptance-budget calculation

For N = W*H, the implementation performs the following work.

| Stage | Work at 4096² | Full-resolution-pass accounting |
|---|---:|---:|
| Resident RGB -> YCoCg | 1 read/write traversal, about 12 integer ops/pixel | 1 pass before pyramid |
| 5/3 pyramid | 2 line transforms per level; sum of level areas is < 4N/3, about 35-55 scalar ops per input pixel including line buffers | pyramid work, not counted as post-pyramid full-resolution pass |
| Detail extraction and quantization | 3 coefficient streams per channel, total 3N coefficients; one branch, signed divide, and 1-5 varint bytes per coefficient | one coefficient traversal; finest-level coefficient area is N but no second full RGB pass |
| Entropy/checksum | FNV byte update for each emitted payload byte; zero-run and zigzag varints | included in the same stream traversal |
| In-memory stream assembly | directory + payload append, proportional to encoded bytes | no image pass |

The effective post-pyramid full-resolution count is two coefficient traversals:
one quantization/bit-width pass and one simultaneous candidate-emission pass for
absolute varints, delta varints, and significance-mask bit packing. It is within
the two-pass limit even when all fine detail layers are retained. No local
optimization iterations are performed; parameter estimation is fixed lifting
plus scalar quantization.

The measured 4096² runs use exactly eight worker threads on the Apple M5 host
listed in `hardware_profile.txt`. The final one-pass candidate selector measures
596-942 ms on synthetic patterns and 480 ms on a bicubic-upscaled Kodak image;
decode is 306-486 ms and 374 ms respectively. The measured peak process
high-water mark is 866 MiB in the synthetic sweep and 528 MiB for the Kodak
run, below the 2 GiB limit. The synthetic high-water mark is conservative
because the benchmark process retains allocator high-water state across several
images.

Entropy coding is intentionally simple rather than arithmetic-coded: each tile
does one zero-run counter update per coefficient and one unsigned-varint or
bit-pack update per nonzero. On the final adaptive Kodak 4096² run the stream is
2,192,313 bytes (1.045 bpp), and the operation count is bounded by two traversals
of the 3N coefficients plus emitted bytes.
