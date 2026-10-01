import time, threading, json
from dbg import call
import importlib.util
src=open("outrocatch.py").read().split("def setup():")[0]
exec(src)   # helpers + act()
call("clear_input"); call("savestate",op="load",slot=1); time.sleep(1.0)
t=threading.Thread(target=act); t.start(); t.join()
print(log)
t0=time.time(); seen=set(); rows=[]
while time.time()-t0<14:
    r=call("cdrom_sector_history",count=64)
    for e in r["entries"]:
        if e["seq"] in seen: continue
        seen.add(e["seq"]); rows.append((round(time.time()-t0,1), e))
    time.sleep(0.3)
import collections
for tt,e in sorted(rows,key=lambda r:r[1]["seq"]):
    pass
sm=collections.Counter((e["xa_submode"],e["xa_audio_delivered"]) for _,e in rows); print(sm)
first=[(tt,e["lba"],e["xa_submode"]) for tt,e in sorted(rows,key=lambda r:r[1]["seq"])]
prev=None
for tt,l,s in first:
    if prev is None or l!=prev+1: print("t=%.1f lba %d submode %s"%(tt,l,s))
    prev=l
print(len(rows),"sectors")
import collections as C2
print(C2.Counter((e["xa_file"],e["xa_channel"],e["xa_coding"]) for _,e in rows if e["xa_audio_delivered"]))
d=[e["lba"] for _,e in rows if e["xa_audio_delivered"]]; print("delivered lba", min(d), max(d), len(d))
print(json.dumps(rows[0][1]) if rows else "")
