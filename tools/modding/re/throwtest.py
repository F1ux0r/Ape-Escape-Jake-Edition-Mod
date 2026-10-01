from PIL import Image
import sys, time; sys.path.insert(0, "re")
from dbg import call
from tap import tap
def glow_on():
    import math
    call("screenshot_hires", path="D:/Ape Escape Recomp/modding/shots/_g.png")
    im = Image.open("D:/Ape Escape Recomp/modding/shots/_g.png").convert("RGB")
    pts = [(int(312 + 14.5 * math.cos(a / 36 * 6.2832)), int(40 + 14.5 * math.sin(a / 36 * 6.2832))) for a in range(36)]
    return sum(1 for p in pts if (lambda r, g, b: g >= 200 and b >= 200)(*im.getpixel(p))) > 18
call("savestate", op="load", slot=9); time.sleep(1.5)
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.3)
for attempt in range(3):
    if glow_on(): break
    tap("tri"); time.sleep(0.3)
print("glow:", glow_on())
name = sys.argv[1] if len(sys.argv) > 1 else "t"
rx, ry = (int(sys.argv[2], 0), int(sys.argv[3], 0)) if len(sys.argv) > 3 else (0x80, 0x00)
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=rx, ry=ry); time.sleep(0.07)
call("clear_input")
T = [float(x) for x in (sys.argv[4].split(',') if len(sys.argv) > 4 else '0.05,0.15,0.3,0.45,0.6,0.75,0.9,1.05,1.2,1.4,1.6,2.0'.split(','))]
t0 = time.time()
for i, t in enumerate(T):
    while time.time() - t0 < t: time.sleep(0.005)
    call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png")
from PIL import Image
ims = [Image.open(f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png") for i in range(len(T))]
W, H = ims[0].size; sheet = Image.new("RGB", (W*4, H*3))
for i, im in enumerate(ims): sheet.paste(im, ((i%4)*W, (i//4)*H))
sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
print("state", call("read_ram", addr=0x800EC23E, len=2)["hex"])
