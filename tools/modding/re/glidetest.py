"""Compare stock vs powered Sky Flyer glides: spin up, then hold forward and
measure airtime and horizontal distance until landing."""
import sys, time, math, struct
sys.path.insert(0, "re")
from dbg import call
from tap import tap
from flyerlook import arm_flyer, glow


def rd(a, n):
    return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])


def flight(powered):
    if powered:
        arm_flyer()
    else:
        call("savestate", op="load", slot=9); time.sleep(1.5)
        call("set_input", buttons=0xFFFF, lx=0x80, ly=0x40, rx=0x80, ry=0x80); time.sleep(0.1)
        call("clear_input"); time.sleep(0.3)
        if rd(0x800EC2D2, 1)[0] != 6:
            tap("circle"); time.sleep(0.4)
    start = struct.unpack("<3h", rd(0x800EC1FC, 6))
    t0 = time.time(); a = 0.0; last = t0
    airborne_from = None; landed_at = None; states = []
    while time.time() - t0 < 14.0:
        now = time.time(); t = now - t0; a += (now - last) * 30.0; last = now
        if t < 1.2:
            call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80,
                 rx=int(128 + 127 * math.cos(a)), ry=int(128 + 127 * math.sin(a)))
        else:
            call("set_input", buttons=0xFFFF, lx=0x80, ly=0x00, rx=0x80, ry=0x80)
        st = struct.unpack("<H", rd(0x800EC23E, 2))[0]
        states.append(st)
        if st in (0x83, 0x84) and airborne_from is None:
            airborne_from = t
        if airborne_from is not None and st not in (0x83, 0x84, 0x24, 0x80) and t > 1.3:
            landed_at = t
            break
    call("clear_input")
    end = struct.unpack("<3h", rd(0x800EC1FC, 6))
    dist = math.hypot(end[0] - start[0], end[2] - start[2])
    air = (landed_at or 14.0) - (airborne_from or 0.0)
    return air, dist, sorted(set(hex(s) for s in states))


for label, powered in (("stock", False), ("powered", True)):
    air, dist, states = flight(powered)
    print(f"{label:8}: airtime {air:.2f}s  distance {dist:.0f}  states {states}")
