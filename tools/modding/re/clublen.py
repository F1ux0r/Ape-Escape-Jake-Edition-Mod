"""Blade length over time: scale of the club draw matrix (a0 at 0x8009ABF4)
before, during and after a 2 s spin from savestate 9."""
import time, math, struct
from dbg import call
rd = lambda a, n: bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
def scale():
    call("fn_clear"); call("fn_filter", lo="0x8009ABF4", hi="0x8009ABF8"); time.sleep(0.05)
    es = call("fn_entry_tail", count=1)["entries"]; call("fn_disable")
    if not es: return None
    m = struct.unpack("<9h", rd(int(es[-1]["a0"], 16), 18))
    rows = [math.sqrt(sum((m[r*3+c]/4096)**2 for c in range(3))) for r in range(3)]
    # undo the per-row (aspect) scale, then the blade axis (model y) length
    y = math.sqrt(sum((m[r*3+1]/4096/rows[r])**2 for r in range(3))) if all(rows) else 0
    return tuple(round(x, 2) for x in rows), round(y, 3)
call("clear_input"); call("savestate", op="load", slot=9); time.sleep(1.2)
print("before", scale())
t0 = time.time(); k = 0
while time.time() - t0 < 2.0:
    a = (time.time() - t0) * 2 * math.pi * 2.5
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=int(128 + 127*math.cos(a)), ry=int(128 + 127*math.sin(a)))
    k += 1
    if k % 15 == 0: print("spin %.1f" % (time.time() - t0), scale())
call("clear_input"); t1 = time.time()
while time.time() - t1 < 2.5:
    print("after %.2f" % (time.time() - t1), scale())
