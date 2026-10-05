"""Sky Flyer flight from savestate 9: spin up, fly forward, screenshot four
moments while airborne (states 0x83/0x84) and tile them into OUT."""
import sys, time, math, struct, subprocess
from dbg import call
from press import tap
OUT = sys.argv[1]
rd = lambda a, n: bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
call("clear_input"); call("savestate", op="load", slot=9); time.sleep(1.5)
if rd(0x800EC2D2, 1)[0] != 6: tap("circle"); time.sleep(0.5)
t0 = time.time(); a = 0; last = t0; k = 0; shots = []
while time.time() - t0 < 5.0:
    now = time.time(); t = now - t0; a += (now - last) * 30; last = now
    if t < 1.2: call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=int(128 + 127 * math.cos(a)), ry=int(128 + 127 * math.sin(a)))
    else: call("set_input", buttons=0xFFFF, lx=0x80, ly=0x00, rx=0x80, ry=0x80)
    if struct.unpack("<H", rd(0x800EC23E, 2))[0] in (0x83, 0x84):
        k += 1
        if k in (8, 11, 14, 17):
            p = OUT.replace(".png", "_%d.png" % k); call("screenshot_hires", path=p); shots.append(p)
call("clear_input")
args = ["ffmpeg", "-v", "error", "-y"]
for p in shots: args += ["-i", p]
args += ["-filter_complex", "".join("[%d]" % i for i in range(len(shots))) + "concat=n=%d:v=1:a=0,tile=%dx1" % (len(shots), len(shots)), OUT]
subprocess.run(args); print(len(shots))
