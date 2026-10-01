import sys, time, math, struct
sys.path.insert(0, "re")
from dbg import call
from nettest import arm_net
from gdbg import dbg
from PIL import Image


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


name = sys.argv[1] if len(sys.argv) > 1 else "grab"
offset = math.radians(float(sys.argv[2])) if len(sys.argv) > 2 else 0.0
print("net powered:", arm_net())
s = spike()
target = min(monkeys(), key=lambda m: math.hypot(m[1][0] - s[0], m[1][2] - s[2]))
print("target", hex(target[0]), target[1], "spike", s)
for _ in range(60):
    s = spike(); m = struct.unpack("<3h", rd(target[0] + 0x1C, 6))
    if math.hypot(m[0] - s[0], m[2] - s[2]) < 1300:
        break
    sx, sy = stick_for(m[0] - s[0], m[2] - s[2])
    call("set_input", buttons=0xFFFF, lx=axis(sx), ly=axis(-sy), rx=0x80, ry=0x80)
    time.sleep(0.1)
call("clear_input"); time.sleep(0.15)
s = spike(); m = struct.unpack("<3h", rd(target[0] + 0x1C, 6))
print("fire at dist", round(math.hypot(m[0] - s[0], m[2] - s[2])), "offset deg", round(math.degrees(offset)))
dx, dz = m[0] - s[0], m[2] - s[2]
dx, dz = dx * math.cos(offset) - dz * math.sin(offset), dx * math.sin(offset) + dz * math.cos(offset)
sx, sy = stick_for(dx, dz)
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=axis(sx), ry=axis(-sy))
time.sleep(0.07); call("clear_input")

T = [float(x) for x in sys.argv[3].split(',')] if len(sys.argv) > 3 else [0.15, 0.3, 0.45, 0.6, 0.8, 1.0, 1.2, 1.4, 1.7, 2.0, 2.5, 3.0]
t0, log = time.time(), []
for i, t in enumerate(T):
    while time.time() - t0 < t:
        time.sleep(0.005)
    call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png")
    d = dbg(); log.append((t, d["phase"], d["monkey"], d["pos"]))
for l in log:
    print(l)
ims = [Image.open(f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png") for i in range(len(T))]
W, H = ims[0].size
sheet = Image.new("RGB", (W * 4, H * 3))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 4) * W, (i // 4) * H))
sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
print("monkey still listed:", any(o == target[0] for o, _ in monkeys()))
time.sleep(1.5); print("net power still on after catch:", dbg()["on"])
