# Grabber depth check: arm the net, face Spike four ways, screenshot each.
import sys, time, math, struct
sys.path.insert(0, "re")
from dbg import call
from nettest import arm_net
import struct
def rd(a, n):
    return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])


def cam():
    b = rd(0x800E78C0, 32)
    return [struct.unpack_from("<3h", b, 6 * r) for r in range(3)]


def spike():
    return struct.unpack("<3h", rd(0x800EC1FC, 6))


def monkeys():
    out, n = [], 0
    node = struct.unpack("<I", rd(0x800B20D8, 4))[0]
    while node and n < 200:
        t, idx, obj, owner, nxt, prv = struct.unpack("<hhIIII", rd(node, 0x14))
        if t == 3 and obj:
            out.append((owner, struct.unpack("<3i", rd(obj, 12))))
        node, n = nxt, n + 1
    return out


def stick_for(dx, dz):
    """Stick (sx right, sy up) whose camera-relative direction is (dx, dz)."""
    R = cam()
    rx, rz, fx, fz = R[0][0], R[0][2], R[2][0], R[2][2]
    det = rx * fz - fx * rz
    sx, sy = (dx * fz - fx * dz) / det, (rx * dz - dx * rz) / det
    l = math.hypot(sx, sy)
    return sx / l, sy / l


def axis(v):
    return max(0, min(255, int(128 + v * 127)))


print("net powered:", arm_net())
pp = []
for name, (dx, dz) in {"away": (0, 1), "toward": (0, -1), "left": (-1, 0), "right": (1, 0)}.items():
    R = cam()
    fx, fz = R[2][0], R[2][2]   # camera forward in world xz
    rx, rz = R[0][0], R[0][2]
    wx, wz = rx * dx + fx * dz, rz * dx + fz * dz
    sx, sy = stick_for(wx, wz)
    call("set_input", buttons=0xFFFF, lx=axis(sx), ly=axis(-sy), rx=0x80, ry=0x80)
    time.sleep(0.12); call("clear_input"); time.sleep(0.6)
    for _ in range(5):
        pp.append(struct.unpack("<I", rd(0x800EC354, 4))[0]); time.sleep(0.011)
    call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/depth_{name}.png")
print("packet ptr samples", [hex(p) for p in pp])
