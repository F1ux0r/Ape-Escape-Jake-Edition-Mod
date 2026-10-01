"""Powered Sky Flyer: carry close-up, then run in a circle (it should pass
behind Spike), as one contact sheet."""
import sys, time, math
sys.path.insert(0, "re")
from dbg import call
from flyerlook import arm_flyer
from PIL import Image, ImageDraw

name = sys.argv[1] if len(sys.argv) > 1 else "frun"
print("powered:", arm_flyer())
time.sleep(0.4)
tiles = []
call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}_carry.png")
im = Image.open(f"D:/Ape Escape Recomp/modding/shots/{name}_carry.png").crop((170, 70, 270, 190)).resize((300, 360), Image.NEAREST)
ImageDraw.Draw(im).text((4, 4), "carry", fill=(255, 255, 0))
tiles.append(im)
t0 = time.time()
for i in range(7):
    a = (time.time() - t0) * 2.6
    call("set_input", buttons=0xFFFF, lx=int(128 + 127 * math.cos(a)), ly=int(128 + 127 * math.sin(a)), rx=0x80, ry=0x80)
    time.sleep(0.16)
    p = f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png"
    call("screenshot_hires", path=p)
    im = Image.open(p).crop((96, 40, 288, 200)).resize((300, 250), Image.NEAREST)
    ImageDraw.Draw(im).text((4, 4), f"run {i}", fill=(255, 255, 0))
    tiles.append(im.resize((300, 360)))
call("clear_input")
sheet = Image.new("RGB", (300 * 4, 360 * 2))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % 4) * 300, (i // 4) * 360))
sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
