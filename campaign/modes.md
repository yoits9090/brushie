# Mode number registry (CAPS campaign)

Allocation is ORCHESTRATOR-AUTHORITATIVE: claim a number here BEFORE shipping.
Collisions have already happened once (mode 16/17 renumber); prevent the next.

## Stream versions
- v1/v2 legacy (40B dir), v3/v4 (20B dir), v5 (16B dir), v6 (unary rate 1/16)
- v7 = rANS backend (coder-lab, in progress)
- v8+ = reserved for the UI/text second representation (geom-lab)

## Detail-band modes (BRUSHIE_ENTROPY / mode byte)
| mode | owner | status |
|---|---:|---|---|
| 3 | coder | v2+ median-pred base / plain detail (shipped) |
| 4 | coder | parent sig+sign (shipped) |
| 5 | coder | parent sig only (shipped) |
| 6 | coder | no parent + class unary (shipped) |
| 7 | coder | GAP base predictor (shipped) |
| 8 | coder | local-k detail (DEFAULT, shipped) |
| 9 | coder | magnitude prediction (shipped) |
| 10 | coder | class contexts (shipped) |
| 11 | coder | zerotree parent-gated (shipped) |
| 12 | coder | 16x16 block flags (shipped) |
| 13 | coder | parent-block mag contexts - REJECTED (+2.75..3.05%), kept env-gated |
| 14 | coder | parent-block value pred - REJECTED (+13.8..16.2%), kept env-gated |
| 15 | coder | energy-bucket sig ctx - REJECTED (+0.64..4.39%), kept env-gated |
| 16 | geom | flat-block base (trial vs 3, shipped in main) |
| 17 | coder | second-order sig ctx - REJECTED (+4.55..5.95%), kept env-gated |
| 24 | coder | tiny-state block mode (mode-12 layout + 1-4B rANS init via k0 flag bits) - SHIPPED v7 (-0.67% chat, -0.02% photos) |
| 18 | coder | (reserved: BRUSHIE_BLOCK size variant - NOT stream-safe yet) |
| 19 | geom | run-mode detail - REJECTED (loses to 12), reverted; never shipped (no streams exist) |
| 24+ | coder | tiny-state block mode (WIP 2026-08-14) - claim before shipping; decoder range 3..23 needs extending |
| 21 | geom | block-flag mode 8x8 blocks (stream-safe via mode byte, shipped with trial) |
| 22 | geom | block-flag mode 32x32 blocks (stream-safe via mode byte, shipped with trial) |
| 23 | geom | block-flag mode 64x64 blocks (stream-safe via mode byte, reserved) |

## UI/text second representation (geom-lab lead, v8)
- 20+ reserved for palette/edge-map/directional modes; claim before shipping.

## Version collisions (registry history)
- v6: claimed by coder-lab (unary rate) — metric-lab's M3 activity probe had
  also used v6; renumbered kVersionActivity=64 on lab/metric only; probe
  REJECTED at metric level, never merged. Rule: claim version numbers in the
  Stream versions section BEFORE shipping.
