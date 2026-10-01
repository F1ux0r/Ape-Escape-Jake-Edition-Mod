# Arm the net, throw at the nearest monkey, log grabber depth stats per sample.
import sys, time, struct
from dbg import call
from nettest import arm_net
D = 0x9F000000
def rd(a, n): return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
print("armed", arm_net()); time.sleep(0.5)
def st():
    b = rd(D + 144, 20); bias, zsf3, mn, mx, zmax = struct.unpack("<5i", b)
    ph = struct.unpack("<i", rd(D + 4, 4))[0]
    return dict(ph=ph, zsf3=zsf3, otz=(mn, mx), zmax=zmax)
print("idle", st())
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=0x80, ry=0x00)  # right stick up = throw forward
time.sleep(0.07); call("clear_input")
for i in range(12):
    time.sleep(0.06); print(round(i*0.06+0.06,2), st())
