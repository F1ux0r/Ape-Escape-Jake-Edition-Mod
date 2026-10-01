import sys, time, struct; sys.path.insert(0, "re")
from dbg import call
def rd(a, n): return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
def snap():
    g = rd(0x800F5120, 0x60); st = struct.unpack("<H", rd(0x800EC23E, 2))[0]
    v34 = struct.unpack("<4h", g[0x34:0x3c]); v3c = struct.unpack("<4h", g[0x3c:0x44])
    ang = struct.unpack("<3h", g[0x28:0x2e])
    return st, ang, v34, v3c
print("idle:", snap())
# flick right stick up (away) for 6 frames; buttons=0xFFFF means none pressed
call("set_input", buttons=0xFFFF, rx=0x80, ry=0x00, lx=0x80, ly=0x80)
for i in range(14):
    print(i, snap()); time.sleep(0.03)
    if i == 5: call("set_input", buttons=0xFFFF, rx=0x80, ry=0x80, lx=0x80, ly=0x80)
call("clear_input")
