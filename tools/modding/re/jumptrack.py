"""Jump (R1) and double jump with the net powered; capture the grabber."""
import sys, time
sys.path.insert(0, "re")
from dbg import call
from nettest import arm_net
from PIL import Image, ImageDraw

R1 = 0x0800
name = sys.argv[1] if len(sys.argv) > 1 else "jump"
arm_net()
shots = []


def snap(i, label):
    path = f"D:/Ape Escape Recomp/modding/shots/{name}_{i}.png"
    call("screenshot_hires", path=path)
    im = Image.open(path).crop((110, 20, 300, 200)).resize((285, 270), Image.NEAREST)
    ImageDraw.Draw(im).text((4, 4), label, fill=(255, 255, 0))
    shots.append(im)


t0 = time.time()
call("set_input", buttons=0xFFFF & ~R1, lx=0x80, ly=0x80, rx=0x80, ry=0x80)
time.sleep(0.1)
call("clear_input")
for i in range(4):
    time.sleep(0.07)
    snap(len(shots), f"jump +{time.time() - t0:.2f}")
call("set_input", buttons=0xFFFF & ~R1, lx=0x80, ly=0x80, rx=0x80, ry=0x80)
time.sleep(0.1)
call("clear_input")
for i in range(4):
    time.sleep(0.07)
    snap(len(shots), f"double +{time.time() - t0:.2f}")
sheet = Image.new("RGB", (285 * 4, 270 * 2))
for i, im in enumerate(shots):
    sheet.paste(im, ((i % 4) * 285, (i // 4) * 270))
sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
print("done")
