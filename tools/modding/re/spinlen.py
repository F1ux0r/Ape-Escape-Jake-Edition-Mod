"""Spin the club for 2 s from savestate 9, then screenshot every ~50 ms for
1.5 s after letting go; tiles the blade area into OUT/sheet.png."""
import sys, os, time, math, subprocess
from dbg import call
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT): os.remove(os.path.join(OUT, f))
call("clear_input"); call("savestate", op="load", slot=9); time.sleep(1.2)
t0 = time.time()
while time.time() - t0 < 2.0:
    a = (time.time() - t0) * 2 * math.pi * 2.5
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=int(128 + 127 * math.cos(a)), ry=int(128 + 127 * math.sin(a)))
    time.sleep(0.02)
call("clear_input"); n = 0; t1 = time.time()
while time.time() - t1 < 1.5:
    call("screenshot_hires", path=f"{OUT}/p{n:03d}.png"); n += 1; time.sleep(0.03)
subprocess.run(["ffmpeg", "-v", "error", "-y", "-start_number", "0", "-i", f"{OUT}/p%03d.png",
                "-vf", "crop=160:150:112:40,tile=10x3", "-frames:v", "1", f"{OUT}/sheet.png"])
print(n)
