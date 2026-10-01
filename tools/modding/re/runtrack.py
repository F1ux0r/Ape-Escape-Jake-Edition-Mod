"""Run Spike in a circle with the net powered and capture frames with each
buffer choice (debug +140: 1 -> flip 1, 2 -> flip 0), then a pause check."""
import sys, time, struct, math
sys.path.insert(0, "re")
from dbg import call
from nettest import arm_net
from tap import tap
from PIL import Image, ImageDraw


def w32(a, v):
    for i, b in enumerate(struct.pack("<I", v)):
        call("write_ram", addr=a + i, val=b)


arm_net()
rows = []
for label, value in (("flip1", 1), ("flip0", 2)):
    w32(0x9F00008C, value)
    ims = []
    t0 = time.time()
    for i in range(6):
        a = (time.time() - t0) * 3.0
        call("set_input", buttons=0xFFFF, lx=int(128 + 127 * math.cos(a)), ly=int(128 + 127 * math.sin(a)), rx=0x80, ry=0x80)
        time.sleep(0.12)
        path = f"D:/Ape Escape Recomp/modding/shots/run_{label}_{i}.png"
        call("screenshot_hires", path=path)
        ims.append(Image.open(path).crop((96, 40, 288, 200)))
    call("clear_input"); time.sleep(0.3)
    row = Image.new("RGB", (192 * 6, 160))
    for i, im in enumerate(ims):
        row.paste(im, (i * 192, 0))
    ImageDraw.Draw(row).text((4, 4), label, fill=(255, 255, 0))
    rows.append(row.resize((192 * 6 * 2 // 2 * 1, 160)))
sheet = Image.new("RGB", (192 * 6, 160 * len(rows)))
for i, r in enumerate(rows):
    sheet.paste(r, (0, i * 160))
sheet = sheet.resize((sheet.width * 2 // 1, sheet.height * 2 // 1), Image.NEAREST)
sheet.save("D:/Ape Escape Recomp/modding/shots/run_sheet.png")
w32(0x9F00008C, 0)

# Pause: Start opens the pause menu; the overlay should vanish.
tap("start"); time.sleep(0.6)
call("screenshot_hires", path="D:/Ape Escape Recomp/modding/shots/paused.png")
tap("start"); time.sleep(0.4)
print("done")
