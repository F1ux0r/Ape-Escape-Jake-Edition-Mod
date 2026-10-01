import time, threading
from dbg import call
src=open("outrocatch.py").read().split("def setup():")[0]
exec(src)
call("clear_input"); call("savestate",op="load",slot=1); time.sleep(1.0)
t=threading.Thread(target=act); t.start(); t.join()
print(log)
t0=time.time(); rows=[]; base=None
while time.time()-t0<8:
    s=call("spu_status")
    rows.append((round(time.time()-t0,3), s["cd_l"], s["cd_r"], s["cd_push_frames"]))
    time.sleep(0.02)
prev=None
for r in rows:
    key=(r[1],r[2], r[3]>0)
    if key!=prev: print(r)
    prev=key
first=[r for r in rows if r[3]>0]
unm=[r for r in rows if r[3]>0 and r[1]!=0]
if first and unm: print("push started t=%.2f, unmuted t=%.2f with %d frames pushed (%.2fs @44.1k)"%(first[0][0],unm[0][0],unm[0][3],unm[0][3]/44100))
