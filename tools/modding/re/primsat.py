"""List GP0 polys of the latest frame whose first vertex lies in a screen box."""
import sys, json
sys.path.insert(0, "re")
from dbg import call
x0, y0, x1, y1 = map(int, sys.argv[1:5])
st = call("gpu_ring_stats"); f = st["newest_frame"] - 2
if call("gpu_frame_dump", frame=f, count=1)["count"] == 0: f += 1
d = call("gpu_frame_dump", frame=f, count=20000)
from collections import Counter
hits = Counter()
for e in d["entries"]:
    op = int(e["op"], 16); w = [int(v, 16) for v in e["w"]]
    if not (0x20 <= op < 0x40) or len(w) < 2: continue
    # vertex positions: depends on gouraud/textured layout
    step = 1 + (1 if op & 4 else 0) + (1 if op & 0x10 else 0)
    vs = []
    i = 1
    nv = 4 if op & 8 else 3
    for k in range(nv):
        if k and op & 0x10: i += 1   # colour word before vertex (gouraud, not first)
        if i >= len(w): break
        x = w[i] & 0xFFFF; y = w[i] >> 16
        x = x - 0x10000 if x & 0x8000 else x; y = y - 0x10000 if y & 0x8000 else y
        vs.append((x, y)); i += 1
        if op & 4: i += 1             # uv word
    if vs and all(x0 <= x <= x1 and y0 <= y <= y1 for x, y in vs):
        hits[(e["func"], e["ra"], e["op"])] += 1
        if hits[(e["func"], e["ra"], e["op"])] <= 2: print(e["op"], e["src"], e["func"], e["ra"], vs, e["w"][:3])
print(f, hits)
