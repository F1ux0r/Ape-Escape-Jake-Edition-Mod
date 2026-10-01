import time, sys, struct
from dbg import call
from keyons import collect
BTN=int(sys.argv[1],0)
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def setup():
    call("clear_input"); call("savestate",op="load",slot=9); time.sleep(1.0)
    call("spu_events_reset")
import threading
def act():
    time.sleep(1.3)
    for _ in range(int(sys.argv[2]) if len(sys.argv)>2 else 2):
        call("set_input",buttons=0xFFFF&~BTN); time.sleep(0.12); call("clear_input"); time.sleep(0.25)
        print("state",hex(struct.unpack("<H",rd(0x800EC1E0+0x5E,2))[0]), "air", struct.unpack("<h",rd(0x800EC1E0+0xEC,2))[0])
threading.Thread(target=act).start()
evs=collect(3.5, setup)
f0=evs[0]["frame"] if evs else 0
for e in evs: print(e["frame"]-f0, e["v"], e["addr"], e["pitch"], e["adsr_lo"], e["adsr_hi"], e["vol_l"], e["vol_r"])
