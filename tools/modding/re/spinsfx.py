import time, threading, math, struct
from dbg import call
from keyons import collect
MUSIC=(0x425C0,0x54D40,0x55330,0x4C290,0x4DA60,0x520B0,0x52E80,0x48F60,0x49C80,0x4BDE0,0x4A880,0x55820,0x41810,0x432D0,0x44340,0x54BD0,0x45670,0x57EE0)
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def setup():
    call("clear_input"); call("savestate",op="load",slot=9); time.sleep(1.0); call("spu_events_reset")
states=set()
def act():
    time.sleep(1.3)
    t0=time.time()
    while time.time()-t0<2.5:
        a=(time.time()-t0)*2*math.pi*2.5
        call("set_input",buttons=0xFFFF,lx=0x80,ly=0x80,rx=int(128+127*math.cos(a)),ry=int(128+127*math.sin(a)))
        states.add(struct.unpack("<H",rd(0x800EC1E0+0x5E,2))[0])
        time.sleep(0.02)
    call("clear_input")
threading.Thread(target=act).start()
evs=collect(4.5, setup)
for e in evs:
    if int(e["addr"],16) not in MUSIC: print(e["frame"], e["v"], e["addr"], e["pitch"], e["vol_l"])
print("states",sorted(hex(s) for s in states))
