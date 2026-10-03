"""New Game from the title save: log XA clips (with time) and a screenshot
every few seconds."""
import sys, time, json
from dbg import call
from press import tap
OUT = sys.argv[1]; SECS = float(sys.argv[2])
call("clear_input"); call("savestate", op="load", slot=0); time.sleep(1.5)
tap("down"); tap("cross")
t0 = time.time(); seen = set(); clips = []; last_shot = -99; i = 0
while time.time() - t0 < SECS:
    t = time.time() - t0
    for e in call("cdrom_sector_history", count=64)["entries"]:
        if e["seq"] in seen or not e["xa_audio_delivered"]: continue
        seen.add(e["seq"]); clips.append((round(t, 1), e["lba"], e["xa_file"], e["xa_channel"], e["xa_submode"]))
    if t - last_shot >= 4:
        call("screenshot_hires", path=f"{OUT}/s{i:03d}.png"); last_shot = t; i += 1
    time.sleep(0.05)
json.dump(clips, open(f"{OUT}/clips.json", "w"))
# summarise runs of the same (file, channel)
runs = []
for t, lba, f, ch, sm in clips:
    if runs and runs[-1][2] == (f, ch) and lba - runs[-1][4] <= 64:
        runs[-1][1] = t; runs[-1][4] = lba; runs[-1][5] += 1
    else:
        runs.append([t, t, (f, ch), lba, lba, 1])
for r in runs: print("t %.1f-%.1f file/ch %s lba %d..%d sectors %d" % tuple(r))
