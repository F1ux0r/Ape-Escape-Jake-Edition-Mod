import sys, time, math; sys.path.insert(0, "re")
from dbg import call
from tap import tap
from PIL import Image
def net_glow():
    call("screenshot_hires", path="D:/Ape Escape Recomp/modding/shots/_n.png")
    im = Image.open("D:/Ape Escape Recomp/modding/shots/_n.png").convert("RGB")
    cx, cy = 312, 72   # Cross slot
    pts = [(int(cx + 14.5*math.cos(a/36*6.2832)), int(cy + 14.5*math.sin(a/36*6.2832))) for a in range(36)]
    return sum(1 for p in pts if (lambda r, g, b: g >= 200 and b >= 200)(*im.getpixel(p))) > 18
def arm_net():
    call("savestate", op="load", slot=9); time.sleep(1.5)
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.3)
    tap("cross"); time.sleep(0.4)
    for _ in range(3):
        if net_glow(): return True
        tap("cross"); time.sleep(0.4)
    return net_glow()
if __name__ == "__main__":
    name = sys.argv[1]; rx, ry = int(sys.argv[2], 0), int(sys.argv[3], 0)
    T = [float(x) for x in sys.argv[4].split(",")]
    print("net powered:", arm_net())
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=rx, ry=ry); time.sleep(0.07); call("clear_input")
    t0 = time.time()
    for i, t in enumerate(T):
        while time.time() - t0 < t: time.sleep(0.005)
        call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png")
    ims = [Image.open(f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png") for i in range(len(T))]
    W, H = ims[0].size; cols = 4; rows = (len(T) + cols - 1) // cols
    sheet = Image.new("RGB", (W*cols, H*rows))
    for i, im in enumerate(ims): sheet.paste(im, ((i % cols)*W, (i // cols)*H))
    sheet.save(f"D:/Ape Escape Recomp/modding/shots/{name}_sheet.png")
