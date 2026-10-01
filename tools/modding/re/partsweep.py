"""Collapse one part of an object at a time via the plugin's debug mask and
capture a contact sheet. Usage: partsweep.py <name> <count> [crop x0,y0,x1,y1]"""
import sys, time, struct
sys.path.insert(0, "re")
from dbg import call
from PIL import Image

MASK_ADDR = 0x9F000060


def w32(a, v):
    for i, b in enumerate(struct.pack("<I", v)):
        call("write_ram", addr=a + i, val=b)


name, count = sys.argv[1], int(sys.argv[2])
crop = tuple(int(x) for x in sys.argv[3].split(",")) if len(sys.argv) > 3 else (130, 60, 290, 200)
shots = []
for i in range(0, count + 1):
    w32(MASK_ADDR, 0 if i == 0 else (1 << i))
    time.sleep(0.25)
    path = f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png"
    call("screenshot_hires", path=path)
    im = Image.open(path).crop(crop)
    shots.append(im.resize((im.width * 2, im.height * 2), Image.NEAREST))
w32(MASK_ADDR, 0)
W, H = shots[0].size
cols = 4
rows = (len(shots) + cols - 1) // cols
sheet = Image.new("RGB", (W * cols, H * rows))
for i, im in enumerate(shots):
    sheet.paste(im, ((i % cols) * W, (i // cols) * H))
sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
print("saved", len(shots), "shots (0 = none hidden)")
