"""List objects seen by the model transforms (plugin debug log) with their
first part's camera-space position and distance to Spike."""
import sys, time, struct, math
sys.path.insert(0, "re")
from dbg import call


def rd(a, n):
    return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])


n = struct.unpack("<I", rd(0x9F00007C, 4))[0]
spike = struct.unpack("<3h", rd(0x800EC1FC, 6))
cam = rd(0x800E78C0, 32)
R = [struct.unpack_from("<3h", cam, 6 * r) for r in range(3)]
t = struct.unpack_from("<3i", cam, 20)
sv = [sum(R[r][c] * spike[c] for c in range(3)) // 4096 + t[r] for r in range(3)]
print("objects", n, "spike view pos", sv)
for i in range(n):
    obj, ra, cnt = struct.unpack("<3I", rd(0x9F000080 + 12 * i, 12))
    parts = struct.unpack("<I", rd(obj + 0x14, 4))[0]
    first = None
    if 0x80000000 <= parts < 0x80200000:
        first = struct.unpack("<3i", rd(parts + 0x20 + 20, 12))
    d = math.dist(first, sv) if first else -1
    print(f"{obj:08X} ra {ra:08X} parts {cnt:3} first-part t {first} dist-to-spike {d:.0f}")
