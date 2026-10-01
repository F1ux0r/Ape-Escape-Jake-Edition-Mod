import time
from dbg import call
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def dump(): return b"".join(rd(0x80000000+o,0x40000) for o in range(0,0x200000,0x40000))
SQ,R3=0x8000,0x0004
def press(b):
    call("set_input", buttons=0xFFFF & ~b, lx=0x80, ly=0x80, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.6)
call("savestate",op="load",slot=9); time.sleep(1.5)
for i,b in enumerate(bytes.fromhex("0003060100ffff01")): call("write_ram",addr=0x800F51A8+i,val=b)
press(SQ)
d=[dump()]
for _ in range(3):
    press(R3); d.append(dump())
# bytes that take a different value at each of 3 steps and return to start after 3 presses
for i in range(len(d[0])):
    v=[x[i] for x in d]
    if len(set(v[:3]))==3 and v[3]==v[0] and max(v)<8:
        print(hex(0x80000000+i), v)
