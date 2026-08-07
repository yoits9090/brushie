# Enterprise harness v3 correction

## Why the old scoreboard was wrong

The prior `scripts/enterprise_eval.py` did **not** calculate local/windowed
MS-SSIM. For each RGB channel and scale it reduced the entire image to one
mean, variance, and covariance, then called that value SSIM. Images with the
same global moments could score well despite blur, shifted edges, banding, or
local structural damage. The codec was tuned against that proxy, so the old
claims ("beats WebP at .970/.985") were not reliable.

Additional fairness bugs:

1. CAPS had 19 quality points; JPEG/WebP had 7 and AVIF 5. Sparse sweeps can
   make a codec look artificially large at a gate because its next available
   point overshoots quality.
2. Pillow WebP used its default method (4), not the stronger method 6;
   JPEG did not use optimize/progressive settings.
3. AVIF used a sparse CRF sweep and no fixed strong preset.
4. Aggregate means from partial coverage were printed beside full-coverage
   codecs as if comparable. This produced absurd-looking JPEG2000 rows (e.g.
   a single easy meme sample represented the codec at .995).
5. CAPS timings were resident-codec time while AVIF/JPEG2000 included process
   startup and file I/O. The old "10x faster" statement mixed scopes.
6. The "1536px expanded" profile used `thumbnail`, so 768px Kodak inputs were
   never 1536px. It was really a native/max-1536 profile, not a 1536px corpus.
7. JPEG2000 was labeled native FFmpeg jpeg2000, but this ffmpeg build exposes the native
   `jpeg2000` encoder, not `libnative FFmpeg jpeg2000`.

## What v3 changes

- `scripts/quality_metrics.py`: local 11x11 box-window SSIM and explicitly
  versioned 5-scale MS-SSIM-form components (fast integral-image box window), on all RGB
  channels; luma MS-SSIM and the legacy proxy are supplemental columns.
- Primary gate is `ms_ssim_windowed`; the legacy proxy can no longer select a
  candidate.
- CAPS, JPEG, and WebP sweep every legal integer quality 1..100. JPEG
  competes sequential/progressive and 4:2:0/4:4:4 optimized modes; WebP uses
  method 6 and a lossless endpoint. AVIF sweeps CRF 1..63 at SVT-AV1 preset 4
  with verified still-image (`avif=1`, all-intra) settings.
- Native FFmpeg JPEG 2000 (not native FFmpeg jpeg2000) competes yuv420p/RGB whole-image
  tiles and adaptively brackets each gate by qscale.
- Progressive results sweep every CAPS quality×layer and decode actual
  physically truncated streams, so a prefix cannot lose to a complete lower-q
  stream merely because q80 was chosen arbitrarily.
- Candidate timing uses wall-clock encode/decode fields and retains CAPS
  resident timing separately as `codec_*_ms`; process/in-process boundaries
  still differ, so cross-codec CPU claims remain withdrawn.
- Coverage denominator comes from the candidate corpus. Partial coverage is
  explicitly `full_coverage=false` and is diagnostic only.
- `tests/test_quality_metrics.py` proves local structural damage is penalized;
  `tests/test_enterprise_eval_helpers.py` proves the primary metric and
  coverage rules.

## Corrected quick result (2 Kodak photos + chat + meme, 512px max)

| Gate | Codec | Coverage | Mean bytes | Mean windowed MS-SSIM |
|---:|---|---:|---:|---:|
| .970 | AVIF | 4/4 | 7,352 | .9728 |
| .970 | WebP | 4/4 | 8,983 | .9753 |
| .970 | CAPS | 4/4 | 11,516 | .9736 |
| .970 | JPEG | 4/4 | 11,565 | .9744 |
| .985 | AVIF | 4/4 | 13,883 | .9869 |
| .985 | WebP | 4/4 | 15,268 | .9867 |
| .985 | JPEG | 4/4 | 20,040 | .9863 |
| .985 | CAPS | 4/4 | 24,444 | .9884 |
| .995 | JPEG | 4/4 | 52,215 | .9959 |
| .995 | CAPS | 4/4 | 78,254 | .9965 |
| .995 | AVIF | 3/4 | 18,584 | .9958 |
| .995 | WebP | 3/4 | 19,602 | .9955 |

This is less flattering but real: CAPS ties strong JPEG at .970, trails WebP
by 28% and AVIF by 57%; at .985 it trails strong JPEG by 22%, WebP by 60%,
and AVIF by 76%. At .995 CAPS has full coverage but needs lossless on one
photo; partial AVIF/WebP rows are not directly comparable.

## Remaining harness work

1. Replace/augment the box-window proxy with SSIMULACRA2 or Butteraugli when a
   trusted binary is available; keep blinded human tests as the sales gate.
2. Add a true high-resolution corpus. The available Kodak sources are
   768x512; available DIV2K LR validation files are ~1020px. Rename the old
   `expanded` profile to `native_expanded` until real 1536px+ sources land.
3. Optionally add jpegli, mozjpeg, and `cjxl` when installed. Do not call
   libjpeg-turbo "the best JPEG" or omit JPEG XL from final claims.
4. Use repeated warm timing runs or native APIs for cross-codec CPU claims.

Old `enterprise_eval_*` CSVs remain historical artifacts and must not be used
for competitive claims. New reports must state `harness=v3-windowed`.
