"""Dump the 1-up shirt model (entry 0x800F4D40): verts, polys, texture."""
import sys, struct, json
sys.path.insert(0, "re")
from dbg import call
from PIL import Image, ImageDraw
OUT = sys.argv[1]
rd = lambda a, n: bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
hdr, cols, polys, verts = struct.unpack("<4I", rd(0x800F4D40, 16))
nv = struct.unpack("<H", rd(hdr + 6, 2))[0]
V = [struct.unpack_from("<4h", rd(verts, nv * 8), 8 * i) for i in range(nv)]
for i, v in enumerate(V): print(i, v)
b = rd(polys, hdr - polys if hdr > polys else 1400)
P = []; i = 0
SIZES = {t: 28 for t in (0x24, 0x25, 0x2C, 0x2D, 0x34, 0x35, 0x3C, 0x3D)}
while i + 4 <= len(b) and b[i + 3] in SIZES and len(P) < 40:
    t = b[i + 3]; n = 4 if t & 8 else 3
    idx = struct.unpack_from(f"<{n}H", b, i + 4)
    o = i + 4 + (8 if n == 4 else 8)
    uv = [struct.unpack_from("<BB", b, o + (0, 4, 8, 10)[k]) for k in range(n)]
    P.append((t, idx, uv, b[i:i + SIZES[t]].hex())); i += SIZES[t]
print(len(P), "polys, bytes", i)
for p in P: print(hex(p[0]), p[1], p[2])
json.dump({"V": V, "P": [[p[0], p[1], p[2]] for p in P]}, open(f"{OUT}/life.json", "w"))
# texture: tpage x 960, 4bpp; region u 0x40..0x80 v 0x38..0x68
def peek(x, y, w, h):
    hx = call("vram_peek", x=x, y=y, w=w, h=h)["hex"]
    return [int(hx[i * 4:i * 4 + 4], 16) for i in range(w * h)]
clut = peek(1008, 219, 16, 1)
print("clut", [hex(c) for c in clut])
tex = [0] * (64 * 256)
for y0 in range(0, 256, 64):
    t = peek(960, y0, 64, 64)
    tex[y0 * 64:(y0 + 64) * 64] = t
im = Image.new("RGB", (256, 256)); px = im.load()
for y in range(256):
    for x in range(256):
        h = tex[y * 64 + x // 4]; c = clut[(h >> (4 * (x % 4))) & 15]
        px[x, y] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
im = im.resize((1024, 1024), Image.NEAREST); d = ImageDraw.Draw(im)
for p in P:
    pts = [(u * 4, v * 4) for u, v in p[2]]
    d.polygon(pts, outline=(0, 255, 0))
im.save(f"{OUT}/life_tex.png")
