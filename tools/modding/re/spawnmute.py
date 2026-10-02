"""Load the warp-in save (menu slot 3 = debug 2) and poll the SPU CD volume
and CD push counter: the 'I'm going for it!' clip must play while muted."""
import time
from dbg import call
call("clear_input"); call("savestate", op="load", slot=2)
t0 = time.time(); rows = []
while time.time() - t0 < 5:
    s = call("spu_status")
    rows.append((round(time.time() - t0, 3), s["cd_l"], s["cd_r"], s["cd_push_frames"]))
    time.sleep(0.01)
prev = None
for r in rows:
    key = (r[1], r[2], r[3] > 0)
    if key != prev: print(r)
    prev = key
pushing = [r for r in rows if r[3] > 0]
if pushing:
    print("CD audio pushed t=%.2f..%.2f" % (pushing[0][0], pushing[-1][0]))
    print("pushed while unmuted:", [r for r in pushing if r[1] or r[2]][:5])
