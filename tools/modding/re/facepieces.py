"""Find the packed 4-bit texture pieces (palette + RLE image) on the disc that
make up a VRAM rectangle, by decoding each candidate and matching it."""
import re, struct, sys
data = open("disc_user_full.bin", "rb").read()
v = open(sys.argv[1], "rb").read()
CLUT = bytes.fromhex(sys.argv[2])
X0, X1 = int(sys.argv[3]), int(sys.argv[4])          # halfword range to search
def unpack(p, count):
    out = []; i = 0
    while i < len(p) and len(out) < count:
        b = p[i]; i += 1; lo, hi = b & 15, b >> 4
        if lo == hi:
            run = (p[i] if i < len(p) else 0) + 2; i += 1
            out += [lo] * run
        else:
            out += [lo, hi]
    return out[:count]
def halfwords(pix, w, h):
    return [pix[k * 4] | pix[k * 4 + 1] << 4 | pix[k * 4 + 2] << 8 | pix[k * 4 + 3] << 12 for k in range(w * h)]
vh = lambda x, y: struct.unpack_from("<H", v, (y * 1024 + x) * 2)[0]
found = {}
for m in re.finditer(re.escape(CLUT), data):
    img = m.start() + 32
    ln, x, y, w, h = struct.unpack_from("<IHHHH", data, img)
    if not (0 < w <= 64 and 0 < h <= 256 and 12 < ln < 0x4000): continue
    hw = halfwords(unpack(data[img + 12:img + ln], w * 4 * h) + [0] * (w * 4 * h), w, h)
    for vy in range(0, 256 - h + 1):
        for vx in range(X0, X1 - w + 1):
            if all(vh(vx + k, vy) == hw[k] for k in range(w)) and \
               all(vh(vx + k, vy + r) == hw[r * w + k] for r in range(h) for k in range(w)):
                found.setdefault((vx, vy, w, h), []).append(hex(m.start() - 8))
for k, vv in sorted(found.items(), key=lambda t: (t[0][1], t[0][0])):
    print(k, vv[:4], len(vv))
