"""New Game from the title save, Start to skip the cutscenes; logs the
warp-in clip's sectors and whether the SPU CD volume was 0 while CD audio
was being pushed."""
import time, threading
from dbg import call
from press import tap
call("clear_input"); call("savestate", op="load", slot=0); time.sleep(1.5)
tap("down"); tap("cross")
def skipper():
    for _ in range(5):
        time.sleep(4); tap("start")
threading.Thread(target=skipper, daemon=True).start()
t0 = time.time(); seen = set(); xa = []; spu = []
while time.time() - t0 < 50:
    t = round(time.time() - t0, 2)
    for e in call("cdrom_sector_history", count=64)["entries"]:
        if e["seq"] not in seen and e["xa_audio_delivered"]:
            seen.add(e["seq"]); xa.append((t, e["lba"], e["xa_channel"], e["xa_submode"]))
    s = call("spu_status"); spu.append((t, s["cd_l"], s["cd_r"], s["cd_push_frames"]))
    time.sleep(0.02)
spawn = [x for x in xa if 51083 <= x[1] <= 51340]
print("warp-in clip sectors:", len(spawn), "channels", sorted(set(x[2] for x in spawn)), "t", spawn[0][0] if spawn else None, "..", spawn[-1][0] if spawn else None)
if spawn:
    lo, hi = spawn[0][0] - 0.3, spawn[-1][0] + 1.2
    win = [r for r in spu if lo <= r[0] <= hi]
    prev = None
    for r in win:
        k = (r[1], r[2])
        if k != prev: print(r)
        prev = k
    pushing = [r for r in win if r[3] > 0]
    print("pushed while unmuted (in window):", [r for r in pushing if (r[1] or r[2]) and r[0] < spawn[-1][0] + 0.4][:5])
