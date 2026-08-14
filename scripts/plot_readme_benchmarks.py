#!/usr/bin/env python3
"""Render the README benchmark image from the checked-in v7 quick aggregate.

The figure intentionally focuses on measured Brushie strengths: the current v7
quick harness beats JPEG 2000 at every proxy gate and is substantially faster
to encode/decode than AVIF and JPEG 2000. JPEG and WebP remain faster in the
same run, and AVIF/WebP remain smaller, so this is not an overall leaderboard.
"""
from pathlib import Path
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "campaign_v7_confirm_aggregate.csv"
rows = list(csv.DictReader(path.open()))
codecs = ["CAPS", "JPEG2000", "AVIF", "WebP", "JPEG"]
colors = {"CAPS":"#1565c0", "AVIF":"#ef5350", "WebP":"#43a047", "JPEG":"#757575", "JPEG2000":"#8e24aa"}
labels = {"CAPS":"Brushie", "AVIF":"AVIF", "WebP":"WebP", "JPEG":"JPEG", "JPEG2000":"JPEG 2000"}
gates = [0.970, 0.985, 0.995]
def row(codec, gate):
    return next(r for r in rows if r["codec"] == codec and abs(float(r["threshold"]) - gate) < 1e-6)
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.5, 4.5), dpi=160)
# Panel A: rate at each quality gate. CAPS is highlighted, with all codecs retained.
x = np.arange(len(gates)); width = .15
for j, codec in enumerate(codecs):
    vals = [float(row(codec,g)["mean_bytes"])/1000 for g in gates]
    bars=ax1.bar(x+(j-2)*width, vals, width*.9, label=labels[codec], color=colors[codec], alpha=1 if codec=="CAPS" else .72, edgecolor="white")
    if codec == "CAPS":
        for b,v in zip(bars,vals): ax1.text(b.get_x()+b.get_width()/2,v+max(vals)*.025,f"{v:.1f}k",ha="center",fontsize=7,color="#1565c0",weight="bold")
ax1.set_xticks(x,[".970",".985",".995"]); ax1.set_ylabel("mean bytes (lower is better)")
ax1.set_title("Current v7 quick gates\nCAPS beats JPEG 2000 at all 3")
ax1.grid(axis="y",alpha=.25);ax1.set_axisbelow(True);ax1.legend(frameon=False,fontsize=8,ncol=2)
# Panel B: timing against heavyweight codecs where CAPS has its clearest speed advantage.
heavy=["CAPS","AVIF","JPEG2000"]; gate=.985; x=np.arange(len(heavy)); w=.33
enc=[float(row(c,gate)["mean_encode_ms"]) for c in heavy]; dec=[float(row(c,gate)["mean_decode_ms"]) for c in heavy]
b1=ax2.bar(x-w/2,enc,w,color=[colors[c] for c in heavy],label="encode")
b2=ax2.bar(x+w/2,dec,w,color=[colors[c] for c in heavy],alpha=.45,label="decode")
ax2.set_xticks(x,[labels[c] for c in heavy]);ax2.set_ylabel("milliseconds (lower is better)");ax2.set_title(".985 quick-gate timing\nCAPS vs AVIF / JPEG 2000")
ax2.grid(axis="y",alpha=.25);ax2.set_axisbelow(True);ax2.legend(frameon=False)
for bs in (b1,b2):
 for b in bs: ax2.text(b.get_x()+b.get_width()/2,b.get_height()+max(enc)*.025,f"{b.get_height():.0f}",ha="center",fontsize=8)
fig.suptitle("Brushie CAPS — measured strengths",fontsize=14,weight="bold")
fig.text(.5,.01,"Quick harness uses local-window MS-SSIM; JPEG/WebP are faster and AVIF/WebP are smaller in this run. See the full reports.",ha="center",fontsize=8,color="#555")
fig.tight_layout(rect=(0,.04,1,.92))
out=ROOT/"docs"/"benchmarks.png";fig.savefig(out,bbox_inches="tight");print(out)
