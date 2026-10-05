"""Textured prims of the last frame inside a screen box: op, clut, tpage,
first-vertex xy, uv, colour. Usage: ringprims.py x0 y0 x1 y1"""
import sys, collections
from dbg import call
x0, y0, x1, y1 = map(int, sys.argv[1:5])
def s16(v): return v - 0x10000 if v & 0x8000 else v
st = call("gpu_ring_stats"); f = st["newest_frame"] - 2
d = call("gpu_frame_dump", frame=f, count=20000)
rows = collections.Counter()
for e in d["entries"]:
    op = int(e["op"], 16); w = [int(v, 16) for v in e["w"]]
    if 0x20 <= op < 0x40 and op & 4:
        gour = op & 0x10; quad = op & 8
        step = 3 if gour else 2
        xy = w[1]; clut = w[2] >> 16; page = (w[2 + step] >> 16) & 0x1FF
        uv = w[2] & 0xFFFF
    elif 0x64 <= op <= 0x7F and op & 4:
        xy = w[1]; clut = w[2] >> 16; page = None; uv = w[2] & 0xFFFF
    else:
        continue
    x, y = s16(xy & 0xFFFF), s16(xy >> 16)
    if x0 <= x <= x1 and y0 <= y <= y1:
        rows[(hex(op), hex(clut), hex(page) if page is not None else "-", hex(w[0] & 0xFFFFFF))] += 1
for k, n in sorted(rows.items()): print(k, n)
