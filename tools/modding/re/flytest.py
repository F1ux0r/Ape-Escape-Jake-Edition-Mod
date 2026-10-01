"""Equip the Circle gadget, spin the right stick to fly, and log Spike's
state and height; capture a few frames."""
import sys, time, math, struct
sys.path.insert(0, "re")
from dbg import call
from tap import tap
from PIL import Image


def rd(a, n):
    return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])


name = sys.argv[1] if len(sys.argv) > 1 else "fly"
spin_time = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
call("savestate", op="load", slot=9); time.sleep(1.5)
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1)
call("clear_input"); time.sleep(0.3)
tap("circle"); time.sleep(0.5)
print("held gadget", rd(0x800EC2D2, 1).hex())
call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}_held.png")

log, shots = [], []
t0 = time.time()
while time.time() - t0 < spin_time + 2.5:
    t = time.time() - t0
    if t < spin_time:
        a = t * 14.0   # fast circles on the right stick
        call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80,
             rx=int(128 + 127 * math.cos(a)), ry=int(128 + 127 * math.sin(a)))
    else:
        call("clear_input")
    st = struct.unpack("<H", rd(0x800EC23E, 2))[0]
    pos = struct.unpack("<3h", rd(0x800EC1FC, 6))
    log.append((round(t, 2), hex(st), pos[1]))
    if len(shots) < 8 and int(t / 0.5) >= len(shots):
        p = f"D:/Ape Escape Recomp/modding/shots/{name}{len(shots)}.png"
        call("screenshot_hires", path=p)
        shots.append(p)
    time.sleep(0.04)
call("clear_input")
for l in log[::4]:
    print(l)
ims = [Image.open(p).crop((96, 0, 288, 200)) for p in shots]
sheet = Image.new("RGB", (192 * 4, 200 * 2))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 4) * 192, (i // 4) * 200))
sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(
    f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
