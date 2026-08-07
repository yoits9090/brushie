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

## Final clean exhaustive quick result

Command: `python3 scripts/enterprise_eval.py --quick --output-prefix harness_v3_final_quick`
from clean git SHA `0561782`; 2,923 encoded candidates; metric
`brushie-box11-ms-ssim-v1`; all codecs 4/4 coverage.

| Gate | Codec | Mean bytes | Mean bpp | Mean windowed MS-SSIM |
|---:|---|---:|---:|---:|
| .970 | AVIF | 6,466 | .296 | .9711 |
| .970 | WebP | 7,658 | .351 | .9716 |
| .970 | JPEG 2000 | 10,046 | .460 | .9706 |
| .970 | optimized JPEG | 10,140 | .465 | .9714 |
| .970 | CAPS | 11,346 | .520 | .9734 |
| .985 | AVIF | 12,948 | .593 | .9856 |
| .985 | WebP | 13,901 | .637 | .9857 |
| .985 | optimized JPEG | 16,328 | .748 | .9853 |
| .985 | JPEG 2000 | 19,084 | .874 | .9853 |
| .985 | CAPS | 19,351 | .887 | .9865 |
| .995 | AVIF | 31,810 | 1.458 | .9953 |
| .995 | optimized JPEG | 32,427 | 1.486 | .9952 |
| .995 | JPEG 2000 | 44,377 | 2.033 | .9954 |
| .995 | WebP | 65,498 | 3.001 | .9964 |
| .995 | CAPS | 77,869 | 3.568 | .9964 |

The old claims are reversed: CAPS trails WebP/AVIF and strong JPEG. The hard
photo (`kodak_02`) drives the .995 deficit: CAPS falls back to its 241,350-byte
lossless stream vs AVIF 78,927 and JPEG 64,394. Progressive streaming is useful
for UI/meme content, but both photos require the complete stream to reach the
.950/.970 windowed gates; see `_previews.csv`.

## Remaining harness work

1. Add SSIMULACRA2 or Butteraugli when a trusted binary is available; keep
   blinded human tests as the sales gate.
2. Add a true >=1536px corpus. Current `native_expanded` sources top out at
   1200px (synthetic), 1020px (DIV2K LR), and 768px (Kodak).
3. Add jpegli, mozjpeg, and `cjxl` when installed; JPEG XL is the true target.
4. Use identical native APIs/boundaries, warmups/repeats, medians, CPU time,
   and fixed resources before any cross-codec cost claim.

Old `enterprise_eval_*` CSVs remain historical artifacts and must not be used
for competitive claims. New reports must state `harness=v3-windowed`.

## Post-audit tuned result (current)

After committing the honest baseline, three corrected-metric codec changes
were made and cleanly rerun from SHA `012f74a` (3,323 candidates, all 4/4):

1. q95+ coarseness exponent 0.8 removes the q99→lossless cliff;
2. encoder competes self-described base targets 32/64 per quality;
3. delivery/high-tier diagonal+chroma weights are separately tuned.

| Gate | AVIF | WebP | strong JPEG | JPEG 2000 | CAPS tuned |
|---:|---:|---:|---:|---:|---:|
| .970 | 6,466 | 7,658 | 10,140 | 10,046 | 10,770 |
| .985 | 12,948 | 13,901 | 16,328 | 19,084 | 18,817 |
| .995 | 31,810 | 65,498 | 32,427 | 44,377 | 38,709 |

vs the corrected baseline CAPS: -5.1%, -2.8%, -50.3%. At .995 CAPS now beats
WebP 38% and JPEG 2000 13%, but remains 22% larger than AVIF and 19% larger
than strong JPEG. At .970 it is within 6-7% of JPEG/JPEG2000 but still far
behind WebP/AVIF. `harness_v3_tuned_quick_*` is the current reference output.
