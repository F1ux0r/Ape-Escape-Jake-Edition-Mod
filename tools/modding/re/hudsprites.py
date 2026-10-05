"""List the HUD sprites (texture page word + 0x64..0x67 sprite) hanging off
table + 0x4000 of the frame buffer last drawn: page, uv, clut, xy, size."""
import struct
from dbg import call
rd = lambda a, n: bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
w = lambda a: struct.unpack("<I", rd(a, 4))[0]
def sprites():
    out = []
    for idx in (0, 1):
        table = 0x800EC3D0 + 0x4078 * idx
        at = w(table + 0x4000) & 0xFFFFFF; n = 0
        while at not in (0, 0xFFFFFF) and n < 512:
            addr = 0x80000000 | at; tag = w(addr); cnt = tag >> 24
            words = struct.unpack("<%dI" % cnt, rd(addr + 4, cnt * 4)) if cnt else ()
            for k in range(1, cnt - 2):
                op = words[k] >> 24; page = words[k - 1]
                if 0x64 <= op <= 0x67 and page >> 24 == 0xE1:
                    xy = words[k + 1]; uvc = words[k + 2]; size = words[k + 3] if op in (0x64, 0x65) else 0
                    out.append(dict(idx=idx, page=hex(page & 0x1FF), uv=hex(uvc & 0xFFFF), clut=hex(uvc >> 16),
                                    x=xy & 0xFFFF, y=xy >> 16, w=size & 0xFFFF, h=size >> 16, col=hex(words[k] & 0xFFFFFF)))
            at = tag & 0xFFFFFF; n += 1
    return out
if __name__ == "__main__":
    seen = set()
    for s in sprites():
        k = (s["page"], s["uv"], s["clut"], s["x"], s["y"])
        if k in seen: continue
        seen.add(k); print(s)
