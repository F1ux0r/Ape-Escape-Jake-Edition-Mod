"""Primitives of a GPU frame-dump whose screen bounding box contains (x, y)."""
def s16(v): return v - 0x10000 if v & 0x8000 else v
def verts(op, w):
    if 0x20 <= op < 0x40:
        n = 4 if op & 8 else 3; tex = op & 4; gour = op & 0x10
        i = 1; out = []
        for k in range(n):
            if k and gour: i += 1
            if i >= len(w): break
            out.append((s16(w[i] & 0xFFFF), s16(w[i] >> 16))); i += 1
            if tex: i += 1
        return out
    if 0x60 <= op < 0x80 and len(w) > 1:
        x, y = s16(w[1] & 0xFFFF), s16(w[1] >> 16)
        size = {0x68: 1, 0x70: 8, 0x78: 16}.get(op & 0x78)
        if size: wd = ht = size
        else:
            sw = w[3] if op & 4 else w[2]; wd, ht = sw & 0xFFFF, sw >> 16
        return [(x, y), (x + wd, y + ht)]
    return []
def at(entries, x, y, pad=4):
    out = []
    for e in entries:
        op = int(e["op"], 16); w = [int(v, 16) for v in e["w"]]
        v = verts(op, w)
        if not v: continue
        xs = [a for a, b in v]; ys = [b for a, b in v]
        if min(xs) - pad <= x <= max(xs) + pad and min(ys) - pad <= y <= max(ys) + pad:
            out.append((e, op, w))
    return out
