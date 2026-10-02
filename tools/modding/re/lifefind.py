"""Hide each candidate model in turn (zero its verts), screenshot, restore."""
import sys, struct, time
sys.path.insert(0, "re")
from dbg import call
from PIL import Image
rd = lambda a, n: bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
OUT = sys.argv[1]
for o in range(0x800F4C80, 0x800F4D50, 0x10):
    w = struct.unpack("<4I", rd(o, 16))
    nv = struct.unpack("<H", rd(w[0] + 6, 2))[0]
    orig = rd(w[3], nv * 8)
    call("write_ram", addr=w[3], hex="00" * len(orig))
    time.sleep(0.25)
    p = f"{OUT}/hide_{o:08X}.png"
    call("screenshot_hires", path=p)
    call("write_ram", addr=w[3], hex=orig.hex())
    Image.open(p).crop((150, 0, 250, 90)).save(p)
