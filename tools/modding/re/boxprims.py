"""Every polygon of a recent frame with a vertex inside a screen box: op, the
raw words, and (for Gouraud ones) each vertex colour. Usage: boxprims.py x0 y0 x1 y1"""
import sys
from dbg import call
x0, y0, x1, y1 = map(int, sys.argv[1:5])
def s16(v): return v - 0x10000 if v & 0x8000 else v
st = call("gpu_ring_stats"); f = st["newest_frame"]
while len(call("gpu_frame_dump", frame=f, count=50)["entries"]) < 20: f -= 1
d = call("gpu_frame_dump", frame=f, count=20000)
for n, e in enumerate(d["entries"]):
    op = int(e["op"], 16); w = [int(v, 16) for v in e["w"]]
    if not 0x20 <= op < 0x40:
        continue
    gour, quad, tex = op & 0x10, op & 8, op & 4
    nv = 4 if quad else 3
    per = 1 + (1 if tex else 0) + (1 if gour else 0)
    xs, cols = [], []
    for v in range(nv):
        base = 1 + v * per if not gour else v * per
        col = (w[0] if v == 0 else w[v * per]) & 0xFFFFFF if gour else w[0] & 0xFFFFFF
        xy = w[base + (1 if gour else 0)] if gour else w[1 + v * per]
        xs.append((s16(xy & 0xFFFF), s16(xy >> 16)))
        cols.append(col)
    if any(x0 <= x <= x1 and y0 <= y <= y1 for x, y in xs):
        print(n, hex(op), xs, ["%06X" % c for c in cols] if gour else "%06X" % cols[0],
              " ".join("%08X" % x for x in w))
