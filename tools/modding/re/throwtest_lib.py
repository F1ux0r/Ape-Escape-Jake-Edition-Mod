import time
from dbg import call
from tap import tap
from PIL import Image
def glow_on():
    import math
    call("screenshot_hires", path="D:/Ape Escape Recomp/modding/shots/_g.png")
    im = Image.open("D:/Ape Escape Recomp/modding/shots/_g.png").convert("RGB")
    pts = [(int(312 + 14.5 * math.cos(a / 36 * 6.2832)), int(40 + 14.5 * math.sin(a / 36 * 6.2832))) for a in range(36)]
    return sum(1 for p in pts if (lambda r, g, b: g >= 200 and b >= 200)(*im.getpixel(p))) > 18
def arm():
    call("savestate", op="load", slot=9); time.sleep(1.5)
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.3)
    for _ in range(3):
        if glow_on(): return True
        tap("tri"); time.sleep(0.3)
    return glow_on()
