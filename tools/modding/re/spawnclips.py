"""Load the warp-in save N times and log which XA clips (file/channel/LBA
range) are delivered in the first seconds."""
import sys, time
from dbg import call
slot = int(sys.argv[1]) if len(sys.argv) > 1 else 2
runs = int(sys.argv[2]) if len(sys.argv) > 2 else 5
for r in range(runs):
    call("clear_input"); call("savestate", op="load", slot=slot)
    t0 = time.time(); rows = {}
    while time.time() - t0 < 5:
        for e in call("cdrom_sector_history", count=64)["entries"]:
            if e["xa_audio_delivered"]:
                rows.setdefault(e["seq"], e)
        time.sleep(0.05)
    clips = {}
    for e in rows.values():
        k = (e["xa_file"], e["xa_channel"])
        lo, hi = clips.get(k, (e["lba"], e["lba"]))
        clips[k] = (min(lo, e["lba"]), max(hi, e["lba"]))
    print(r, clips)
