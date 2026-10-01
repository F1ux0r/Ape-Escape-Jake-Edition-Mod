"""Compare the net grip-turn options (debug +108: 4 = none, 1..3 = flips)."""
import sys, time, struct
sys.path.insert(0, "re")
from dbg import call
from PIL import Image


def w32(a, v):
    for i, b in enumerate(struct.pack("<I", v)):
        call("write_ram", addr=a + i, val=b)


ims = []
for value in (4, 1, 2, 3):
    w32(0x9F00006C, value)
    time.sleep(0.3)
    path = f"D:/Ape Escape Recomp/modding/shots/flip{value}.png"
    call("screenshot_hires", path=path)
    ims.append(Image.open(path).crop((110, 40, 290, 200)).resize((360, 320), Image.NEAREST))
w32(0x9F00006C, 0)
sheet = Image.new("RGB", (720, 640))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 2) * 360, (i // 2) * 320))
sheet.save("D:/Ape Escape Recomp/modding/shots/flip_sheet.png")
print("order: none, flip1, flip2, flip3")
