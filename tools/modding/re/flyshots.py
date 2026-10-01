"""Power the Sky Flyer, fly, and capture frames of it in the air."""
import sys, time, math
sys.path.insert(0, "re")
from dbg import call
from flyerlook import arm_flyer
from PIL import Image

name = sys.argv[1] if len(sys.argv) > 1 else "air"
print("powered:", arm_flyer())
shots = []
t0 = time.time(); a = 0.0; last = t0
while time.time() - t0 < 2.4:
    now = time.time(); t = now - t0; a += (now - last) * 30.0; last = now
    if t < 1.6:
        call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80,
             rx=int(128 + 127 * math.cos(a)), ry=int(128 + 127 * math.sin(a)))
    else:
        call("clear_input")
    if len(shots) < 6 and t > 0.3 + 0.32 * len(shots):
        p = f"D:/Ape Escape Recomp/modding/shots/{name}{len(shots)}.png"
        call("screenshot_hires", path=p)
        shots.append(Image.open(p).crop((96, 0, 288, 180)))
call("clear_input")
sheet = Image.new("RGB", (192 * 3, 180 * 2))
for i, im in enumerate(shots):
    sheet.paste(im, ((i % 3) * 192, (i // 3) * 180))
sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(
    f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
