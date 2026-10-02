"""press.py BUTTON... - tap PSX pad buttons via the debug server (active-low)."""
import sys, time
from dbg import call
BITS = {"select": 0, "l3": 1, "r3": 2, "start": 3, "up": 4, "right": 5, "down": 6, "left": 7,
        "l2": 8, "r2": 9, "l1": 10, "r1": 11, "triangle": 12, "circle": 13, "cross": 14, "square": 15}
def tap(name, hold=0.12, gap=0.35):
    call("set_input", buttons=0xFFFF & ~(1 << BITS[name]), lx=0x80, ly=0x80, rx=0x80, ry=0x80)
    time.sleep(hold); call("clear_input"); time.sleep(gap)
if __name__ == "__main__":
    for b in sys.argv[1:]: tap(b)
