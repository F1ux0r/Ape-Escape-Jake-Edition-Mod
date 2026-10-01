# Extract a file from a MODE2/2352 raw PS1 disc image (ISO9660).
import struct, sys
BIN = r"D:/Ape Escape (USA)/Ape Escape (USA).bin"
f = open(BIN, "rb")
def sector(lba):
    f.seek(lba * 2352 + 24)
    return f.read(2048)
pvd = sector(16)
root = pvd[156:156+34]
def entries(lba, size):
    out = []
    for i in range((size + 2047)//2048):
        d = sector(lba + i); p = 0
        while p < 2048 and d[p]:
            n = d[p]; e = d[p:p+n]
            elba = struct.unpack_from("<I", e, 2)[0]; esz = struct.unpack_from("<I", e, 10)[0]
            name = e[33:33+e[32]].decode("ascii", "replace")
            out.append((name, elba, esz, e[25] & 2)); p += n
    return out
rl, rs = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
ents = entries(rl, rs)
for n, l, s, d in ents: print(f"{n:30} lba={l:7} size={s:9} {'DIR' if d else ''}")
want = sys.argv[1] if len(sys.argv) > 1 else "SCUS_944.23;1"
for n, l, s, d in ents:
    if n == want:
        data = b"".join(sector(l + i) for i in range((s + 2047)//2048))[:s]
        open("re/" + n.split(";")[0], "wb").write(data); print("wrote", n, len(data))
