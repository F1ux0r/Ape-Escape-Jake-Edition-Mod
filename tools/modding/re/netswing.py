import time, threading
from dbg import call
from keyons import collect
from tap import tap
MUSIC=(0x425C0,0x54D40,0x55330,0x4C290,0x4DA60,0x520B0,0x52E80,0x48F60,0x49C80,0x4BDE0,0x4A880,0x55820,0x41810,0x432D0,0x44340,0x54BD0,0x45670,0x57EE0)
def setup():
    call("clear_input"); call("savestate",op="load",slot=9); time.sleep(1.0); call("spu_events_reset")
def act():
    time.sleep(1.2); tap("cross"); time.sleep(0.8)
    print("held", call("read_ram",addr=0x800EC2D2,len=1)["hex"])
    call("set_input",buttons=0xFFFF,lx=0x80,ly=0x80,rx=0x80,ry=0xFF); time.sleep(0.08); call("clear_input")
    print("swing")
threading.Thread(target=act).start()
for e in collect(4.0, setup):
    if int(e["addr"],16) not in MUSIC: print(e["frame"], e["v"], e["addr"], e["pitch"])
