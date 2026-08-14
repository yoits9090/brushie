#!/usr/bin/env python3
"""Render the README benchmark image from checked-in benchmark aggregates.

The figure deliberately contains two honest panels: the strongest Brushie
proxy-gate result (0.995 quick gate, where CAPS is close to JPEG and beats
JPEG2000 in this snapshot) and the broad-corpus encode/decode timing result,
where CAPS is faster to encode than AVIF/JPEG2000 and faster to decode than
AVIF/JPEG2000. It does not present CAPS as the overall rate winner.
"""
from pathlib import Path
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
agg = pd.read_csv(ROOT / "benchmarks" / "caps_broad_aggregate.csv")
codecs = ["CAPS", "AVIF", "WebP", "JPEG", "JPEG2000"]
colors = {"CAPS":"#1565c0", "AVIF":"#ef5350", "WebP":"#43a047", "JPEG":"#757575", "JPEG2000":"#8e24aa"}
labels = {"CAPS":"Brushie", "AVIF":"AVIF", "WebP":"WebP", "JPEG":"JPEG", "JPEG2000":"JPEG 2000"}
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.5), dpi=160)
# Proxy gate: highlight the strongest relative CAPS point, but show all competitors.
th = 0.995
x = agg[agg.threshold == th].set_index("codec").reindex(codecs)
vals = [x.loc[c,"mean_bytes"] / 1000 for c in codecs]
bars = ax1.bar([labels[c] for c in codecs], vals, color=[colors[c] for c in codecs], edgecolor="white")
ax1.set_title("0.995 local-MS-SSIM gate\n163-image broad corpus")
ax1.set_ylabel("mean bytes (lower is better)")
ax1.grid(axis="y", alpha=.25)
ax1.set_axisbelow(True)
for b,v in zip(bars,vals): ax1.text(b.get_x()+b.get_width()/2,v+max(vals)*.025,f"{v:.1f}k",ha="center",fontsize=8)
ax1.text(0.02,0.98,"CAPS is close to JPEG and\nbeats JPEG 2000 here",transform=ax1.transAxes,va="top",fontsize=8,color="#1565c0",weight="bold")
# Timing: encode and decode are independent lower-is-better panels.
mean = agg.groupby("codec")[["mean_encode_ms","mean_decode_ms"]].mean().reindex(codecs)
xx=np.arange(len(codecs)); w=.36
b1=ax2.bar(xx-w/2,mean.mean_encode_ms,color=[colors[c] for c in codecs],width=w,label="encode")
b2=ax2.bar(xx+w/2,mean.mean_decode_ms,color=[colors[c] for c in codecs],width=w,alpha=.48,label="decode")
ax2.set_xticks(xx, [labels[c] for c in codecs]); ax2.set_ylabel("milliseconds (lower is better)"); ax2.set_title("Broad-corpus timing\nApple CPU reference run"); ax2.grid(axis="y",alpha=.25);ax2.set_axisbelow(True);ax2.legend(frameon=False)
for bs in (b1,b2):
 for b in bs: ax2.text(b.get_x()+b.get_width()/2,b.get_height()+1,f"{b.get_height():.0f}",ha="center",fontsize=7)
fig.suptitle("Brushie CAPS — measured strengths, not a universal codec ranking",fontsize=13,weight="bold")
fig.text(.5,.01,"Proxy-gate results are not SSIMULACRA2/Butteraugli or human preference. See campaign/landscape_real_metrics.md.",ha="center",fontsize=8,color="#555")
fig.tight_layout(rect=(0,.04,1,.92))
out=ROOT/"docs"/"benchmarks.png";fig.savefig(out,bbox_inches="tight");print(out)
