"""Trace height / vertical speed / state through a Sky Flyer flight.
usage: flytrace.py [stock|powered] [spin_seconds]"""
import sys, time, math, struct
sys.path.insert(0, "re")
from dbg import call
from tap import tap
from flyerlook import arm_flyer

def rd(a, n): return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])

def trace(powered, spin=1.2, total=7.0, fwd=True):
    if powered:
        arm_flyer()
    else:
        call("savestate", op="load", slot=9); time.sleep(1.5)
        call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1)
        call("clear_input"); time.sleep(0.3)
        if rd(0x800EC2D2, 1)[0] != 6: tap("circle"); time.sleep(0.4)
    b = rd(0x800EC1E0, 0x150)
    x0, y0, z0 = struct.unpack_from("<3h", b, 0x1C)
    out = []; t0 = time.time(); a = 0.0; last = t0
    while time.time() - t0 < total:
        now = time.time(); t = now - t0; a += (now - last) * 30.0; last = now
        if t < spin:
            call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80,
                 rx=int(128 + 127 * math.cos(a)), ry=int(128 + 127 * math.sin(a)))
        else:
            call("set_input", buttons=0xFFFF, lx=0x80, ly=0x00 if fwd else 0x80, rx=0x80, ry=0x80)
        b = rd(0x800EC1E0, 0x150)
        x, y, z = struct.unpack_from("<3h", b, 0x1C)
        st = struct.unpack_from("<H", b, 0x5E)[0]
        vy = struct.unpack_from("<i", b, 0x124)[0] / 4096.0
        air = struct.unpack_from("<h", b, 0xEC)[0] < 0
        out.append((t, st, y0 - y, vy, math.hypot(x - x0, z - z0), air))
        if t > spin + 0.3 and not air and st not in (0x80, 0x81, 0x82, 0x83, 0x84): break
    call("clear_input")
    return out

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "stock"
    spin = float(sys.argv[2]) if len(sys.argv) > 2 else 1.2
    rows = trace(mode == "powered", spin)
    peak = max(r[2] for r in rows)
    print(f"{mode}: samples {len(rows)} peak height {peak} dist {rows[-1][4]:.0f} airtime~{rows[-1][0]:.2f}")
    for r in rows[::2]:
        print(f"{r[0]:5.2f} st {r[1]:#04x} h {r[2]:5d} vy {r[3]:7.2f} d {r[4]:6.0f} {'A' if r[5] else '-'}")
