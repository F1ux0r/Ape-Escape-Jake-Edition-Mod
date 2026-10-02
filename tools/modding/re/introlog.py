"""From the title save, start a New Game and log the intro: every ~1.5 s a
screenshot plus which skinned objects the transform saw (and their mesh
header bytes)."""
import sys, time, struct, collections, json
from dbg import call
from press import tap
OUT = sys.argv[1]; SECS = float(sys.argv[2])
rd = lambda a, n: bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
call("clear_input"); call("savestate", op="load", slot=0); time.sleep(1)
tap("down"); tap("cross")
t0 = time.time(); i = 0; log = []
while time.time() - t0 < SECS:
    call("fn_clear"); call("fn_filter", lo="0x80016A60", hi="0x80016A64"); time.sleep(0.25)
    es = call("fn_entry_tail", count=200)["entries"]; call("fn_disable")
    objs = {}
    for o in sorted(set(int(e["a0"], 16) for e in es)):
        h = struct.unpack("<I", rd(o, 4))[0]
        objs[hex(o)] = [hex(h), rd(h, 12).hex() if h >> 24 == 0x80 else ""]
    call("screenshot_hires", path=f"{OUT}/f{i:03d}.png")
    log.append({"i": i, "t": round(time.time() - t0, 1), "objs": objs}); i += 1
    time.sleep(1.0)
json.dump(log, open(f"{OUT}/log.json", "w"), indent=1)
