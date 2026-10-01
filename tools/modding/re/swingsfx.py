import time, threading, sys
from dbg import call
from keyons import collect
MUSIC=(0x425C0,0x54D40,0x55330,0x4C290,0x4DA60,0x520B0,0x52E80)
flicks=int(sys.argv[1])
def setup():
    call("clear_input"); call("savestate",op="load",slot=9); time.sleep(1.0); call("spu_events_reset")
def act():
    time.sleep(1.3)
    for _ in range(flicks):
        call("set_input",buttons=0xFFFF,rx=0x00,ry=0x80,lx=0x80,ly=0x80); time.sleep(0.15); call("clear_input"); time.sleep(0.8)
threading.Thread(target=act).start()
evs=collect(1.3+flicks*1.0+0.8, setup)
for e in evs:
    if int(e["addr"],16) not in MUSIC: print(e["frame"], e["v"], e["addr"], e["pitch"], e["vol_l"])
print("held", call("read_ram",addr=0x800EC2D2,len=1)["hex"])
