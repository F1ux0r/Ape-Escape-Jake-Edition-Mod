import time, json, sys
from dbg import call
def collect(seconds, setup=None):
    if setup: setup()
    t0=time.time(); seen={}
    while time.time()-t0<seconds:
        r=call("spu_events",count=4096)
        for e in r["events"]:
            if e["kind"]=="KEYON": seen[e["seq"]]=e
        time.sleep(0.2)
    return [seen[k] for k in sorted(seen)]
if __name__=="__main__":
    slot=int(sys.argv[1]); secs=float(sys.argv[2])
    call("clear_input"); call("savestate",op="load",slot=slot); time.sleep(0.2)
    evs=collect(secs)
    f0=evs[0]["frame"] if evs else 0
    for e in evs:
        print(e["frame"]-f0, e["v"], e["addr"], e["pitch"], e["adsr_lo"], e["adsr_hi"], e["vol_l"], e["vol_r"])
