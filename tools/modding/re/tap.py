import sys, time; sys.path.insert(0, "re")
from dbg import call
BTN = {"tri": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000, "start": 0x0008}
def tap(name, hold=0.12):
    call("set_input", buttons=0xFFFF & ~BTN[name], lx=0x80, ly=0x80, rx=0x80, ry=0x80); time.sleep(hold)
    call("clear_input"); time.sleep(0.15)
if __name__ == "__main__":
    for n in sys.argv[1:]: tap(n)
