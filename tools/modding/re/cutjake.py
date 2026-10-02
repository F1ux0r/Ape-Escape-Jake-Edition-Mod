"""Parse a mesh in a RAM dump: header, sections, textured polys' pages/cluts/uv extents."""
import struct, sys, collections
ram = open(sys.argv[1], "rb").read(); hdr = int(sys.argv[2], 16) & 0x1FFFFF
g16 = lambda a: struct.unpack_from("<H", ram, a)[0]
ncol, npoly, nparts, nverts = g16(hdr + 8), g16(hdr + 2), g16(hdr + 4), g16(hdr + 6)
print("header", ram[hdr:hdr + 12].hex(), "colours", ncol, "polys", npoly, "parts", nparts, "verts", nverts)
colours = hdr + 12; parts = colours + ncol * 4; verts = parts + nparts * 12; polys = verts + nverts * 8
print("offsets: colours +0x%X parts +0x%X verts +0x%X polys +0x%X" % (colours - hdr, parts - hdr, verts - hdr, polys - hdr))
at = polys; pages = collections.defaultdict(lambda: [999, 999, 0, 0, 0])
for i in range(npoly):
    code = ram[at + 3] & 0xFC
    tex = code in (0x24, 0x2C, 0x34, 0x3C); size = 0x1C if tex else 0x10
    if (code & 0xE0) != 0x20: print("bad code at", i, hex(at)); break
    if tex:
        clut, tp = g16(at + 14), g16(at + 18)
        uvs = [(ram[at + o], ram[at + o + 1]) for o in ((12, 16, 20, 22) if code in (0x2C, 0x3C) else (12, 16, 20))]
        p = pages[(hex(tp & 0x1FF), hex(clut))]
        for u, v in uvs: p[0] = min(p[0], u); p[1] = min(p[1], v); p[2] = max(p[2], u); p[3] = max(p[3], v)
        p[4] += 1
    at += size
print("mesh size 0x%X" % (at - hdr))
for k, v in pages.items(): print("page/clut", k, "u %d..%d v %d..%d polys %d" % tuple(v))
print("parts:", [struct.unpack_from("<HHhhhH", ram, parts + 12 * i) for i in range(nparts)][:3], "...")
