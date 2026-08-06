#!/usr/bin/env python3
"""Low-cost ablations that remain inside the analytical CPU design."""
from __future__ import annotations
import csv, math, re, subprocess, tempfile
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]; CLI=ROOT/'build/brushie'
def metric(a,b):
 m=np.mean((a.astype(float)-b.astype(float))**2); return 99 if m==0 else 10*math.log10(255**2/m)
def val(text,key): return float(re.search(rf'{key}=([0-9.]+)',text).group(1))
src=np.asarray(Image.open(ROOT/'datasets/kodak/PhotoCD_PCD0992/01.png').convert('RGB').resize((512,512),Image.Resampling.LANCZOS),dtype=np.uint8)
rows=[]
with tempfile.TemporaryDirectory(prefix='brushie-ablation-') as td:
 td=Path(td); ppm=td/'source.ppm'; ppm.write_bytes(b'P6\n512 512\n255\n'+src.tobytes())
 for quality in (100,90,82,70,50):
  for tile in (16,32,64):
   caps=td/f'{quality}-{tile}.caps'; out=td/f'{quality}-{tile}.ppm'
   enc=subprocess.run([str(CLI),'encode',str(ppm),str(caps),str(quality),'8',str(tile)],capture_output=True,text=True,check=True)
   subprocess.run([str(CLI),'decode',str(caps),str(out),'512','512','-1'],capture_output=True,text=True,check=True)
   rec=np.asarray(Image.open(out).convert('RGB'),dtype=np.uint8)
   rows.append(dict(quality=quality,tile=tile,encode_ms=val(enc.stdout,'encode_ms'),bytes=caps.stat().st_size,bpp=caps.stat().st_size*8/(512*512),psnr=metric(src,rec)))
with (ROOT/'ablation_results.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
print(f'wrote {len(rows)} ablation rows')
