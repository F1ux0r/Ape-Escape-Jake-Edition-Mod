"""Try fist offsets (left-hand bone space) for the net grip and label each."""
import sys, time, struct
sys.path.insert(0, "re")
from dbg import call
from PIL import Image, ImageDraw


def w32(a, v):
    for i, b in enumerate(struct.pack("<i", v)):
        call("write_ram", addr=a + i, val=b)


offsets = [(0, 0, 0), (40, 0, 0), (-40, 0, 0), (0, 40, 0), (0, -40, 0), (0, 0, 40), (0, 0, -40), (25, 25, 0)]
if len(sys.argv) > 1:
    offsets = [tuple(int(v) for v in o.split(",")) for o in sys.argv[1:]]
w32(0x9F000088, 1)
ims = []
for o in offsets:
    for i, v in enumerate(o):
        w32(0x9F000078 + 4 * i, v)
    time.sleep(0.3)
    path = "D:/Ape Escape Recomp/modding/shots/fist_%d_%d_%d.png" % o
    call("screenshot_hires", path=path)
    im = Image.open(path).crop((120, 80, 240, 190)).resize((360, 330), Image.NEAREST)
    ImageDraw.Draw(im).text((4, 4), "fist %s" % (o,), fill=(255, 255, 0))
    ims.append(im)
w32(0x9F000088, 0)
cols = 4
sheet = Image.new("RGB", (360 * cols, 330 * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % cols) * 360, (i // cols) * 330))
sheet.save("D:/Ape Escape Recomp/modding/shots/fist_sheet.png")
