"""Equip the Sky Flyer (Circle), capture it stock and powered."""
import sys, time, math
sys.path.insert(0, "re")
from dbg import call
from tap import tap
from PIL import Image, ImageDraw


def glow(cx=338, cy=56):
    call("screenshot_hires", path="D:/Ape Escape Recomp/modding/shots/_f.png")
    im = Image.open("D:/Ape Escape Recomp/modding/shots/_f.png").convert("RGB")
    pts = [(int(cx + 14.5 * math.cos(a / 36 * 6.2832)), int(cy + 14.5 * math.sin(a / 36 * 6.2832))) for a in range(36)]
    return sum(1 for p in pts if (lambda r, g, b: g >= 200 and b >= 200)(*im.getpixel(p))) > 18


def arm_flyer():
    call("savestate", op="load", slot=9); time.sleep(1.5)
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1)
    call("clear_input"); time.sleep(0.3)
    if bytes.fromhex(call("read_ram", addr=0x800EC2D2, len=1)["hex"])[0] != 6:
        tap("circle"); time.sleep(0.4)
    for _ in range(3):
        if glow():
            return True
        tap("circle"); time.sleep(0.4)
    return glow()


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "flyer"
    call("savestate", op="load", slot=9); time.sleep(1.5)
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1)
    call("clear_input"); time.sleep(0.3)
    if bytes.fromhex(call("read_ram", addr=0x800EC2D2, len=1)["hex"])[0] != 6:
        tap("circle"); time.sleep(0.5)
    call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}_stock.png")
    print("powered:", arm_flyer()); time.sleep(0.4)
    call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}_power.png")
    tiles = []
    for tag in ("stock", "power"):
        im = Image.open(f"D:/Ape Escape Recomp/modding/shots/{name}_{tag}.png").crop((100, 20, 300, 200)).resize((400, 360), Image.NEAREST)
        ImageDraw.Draw(im).text((4, 4), tag, fill=(255, 255, 0))
        tiles.append(im)
    sheet = Image.new("RGB", (800, 360))
    sheet.paste(tiles[0], (0, 0)); sheet.paste(tiles[1], (400, 0))
    sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_compare.png")
