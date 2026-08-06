#!/usr/bin/env python3
"""Hostile-pattern sweep for failure-mode classification."""
from __future__ import annotations
import csv, math, re, subprocess, tempfile
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "brushie"

def psnr(a, b):
    mse = np.mean((a.astype(np.float64)-b.astype(np.float64))**2)
    return 99.0 if mse == 0 else 10*math.log10(255**2/mse)

def make(name, n):
    yy, xx = np.mgrid[:n, :n]
    if name == 'checker':
        v = ((xx//4 + yy//4)&1)*255; return np.repeat(v[...,None],3,axis=2).astype(np.uint8)
    if name == 'one_pixel_lines':
        v = ((xx%7==0)|(yy%11==0))*255; return np.stack([v, np.roll(v,1,0), np.roll(v,1,1)],-1).astype(np.uint8)
    if name == 'noise':
        rng=np.random.default_rng(7); return rng.integers(0,256,(n,n,3),dtype=np.uint8)
    if name == 'gradient':
        return np.stack([(xx*255//max(1,n-1)),(yy*255//max(1,n-1)),((xx+yy)*255//max(1,2*n-2))],-1).astype(np.uint8)
    if name == 'texture':
        v=(127+60*np.sin(xx/3)+40*np.sin(yy/5)+30*np.sin((xx+yy)/11)).clip(0,255).astype(np.uint8)
        return np.stack([v,np.roll(v,2,0),np.roll(v,3,1)],-1)
    if name == 'edge':
        v=np.where(xx+yy<n,20,235).astype(np.uint8); return np.stack([v, np.roll(v,3,0), np.roll(v,4,1)],-1)
    if name == 'text_ui':
        im=Image.new('RGB',(n,n),(245,245,245)); d=ImageDraw.Draw(im)
        for y in range(20,n,42): d.line((10,y,n-10,y),fill=(30,30,30),width=1)
        d.rectangle((25,25,n-25,n-25),outline=(20,70,180),width=3)
        d.text((40,n//2-10),'CAPS / UI 123',fill=(10,10,10))
        return np.asarray(im,dtype=np.uint8)
    raise ValueError(name)

def parse(t,k):
    m=re.search(rf'{k}=([0-9.]+)',t); return float(m.group(1))

rows=[]
with tempfile.TemporaryDirectory(prefix='brushie-hostile-') as td:
    td=Path(td)
    for n in (512,1024):
        for name in ('checker','one_pixel_lines','noise','gradient','texture','edge','text_ui'):
            arr=make(name,n); ppm=td/f'{name}-{n}.ppm'; caps=td/f'{name}-{n}.caps'; out=td/f'{name}-{n}.out.ppm'
            ppm.write_bytes(f'P6\n{n} {n}\n255\n'.encode()+arr.tobytes())
            enc=subprocess.run([str(CLI),'encode',str(ppm),str(caps),'82','8'],capture_output=True,text=True,check=True)
            dec=subprocess.run([str(CLI),'decode',str(caps),str(out),str(n),str(n),'-1'],capture_output=True,text=True,check=True)
            rec=np.asarray(Image.open(out).convert('RGB'),dtype=np.uint8)
            rows.append(dict(pattern=name,size=n,encode_ms=parse(enc.stdout,'encode_ms'),decode_ms=parse(dec.stdout,'decode_ms'),bytes=caps.stat().st_size,bpp=caps.stat().st_size*8/(n*n),psnr=psnr(arr,rec),failure='high-frequency residual' if name in ('noise','checker','one_pixel_lines','texture') else ''))
with (ROOT/'hostile_results.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=rows[0].keys()); w.writeheader(); w.writerows(rows)
print(f'wrote {len(rows)} hostile rows')
