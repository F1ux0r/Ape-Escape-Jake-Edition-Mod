import sys, time, struct
sys.path.insert(0, "re")
from dbg import call
from PIL import Image, ImageDraw
def w32(a, v):
    for i, b in enumerate(struct.pack("<I", v)): call("write_ram", addr=a + i, val=b)
combos = [(1000, 1000), (500, 500), (300, 350), (200, 250), (100, 150), (1, 1)]
ims = []
for depth, length in combos:
    w32(0x9F000070, depth); w32(0x9F000074, length); time.sleep(0.3)
    path = f"D:/Ape Escape Recomp/modding/shots/bag_d{depth}_l{length}.png"
    call("screenshot_hires", path=path)
    im = Image.open(path).crop((110, 60, 260, 200)).resize((300, 280), Image.NEAREST)
    ImageDraw.Draw(im).text((4, 4), f"depth {depth/1000} len {length/1000}", fill=(255, 255, 0))
    ims.append(im)
w32(0x9F000070, 0); w32(0x9F000074, 0)
sheet = Image.new("RGB", (900, 560))
for i, im in enumerate(ims): sheet.paste(im, ((i % 3) * 300, (i // 3) * 280))
sheet.save("D:/Ape Escape Recomp/modding/shots/bag_sweep.png")
