import time, math, struct, threading
from dbg import call
from tap import tap
from keyons import collect
MUSIC=(0x425C0,0x54D40,0x55330,0x4C290,0x4DA60,0x520B0,0x52E80,0x48F60,0x49C80,0x4BDE0,0x4A880,0x55820,0x41810,0x432D0,0x44340,0x54BD0,0x45670,0x57EE0)
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def cam():
    b=rd(0x800E78C0,32); return [struct.unpack_from("<3h",b,6*r) for r in range(3)]
def spike(): return struct.unpack("<3h", rd(0x800EC1FC,6))
def monkeys():
    out,n=[],0; node=struct.unpack("<I",rd(0x800B20D8,4))[0]
    while node and n<200:
        t,idx,obj,owner,nxt,prv=struct.unpack("<hhIIII",rd(node,0x14))
        if t==3 and obj: out.append(owner)
        node,n=nxt,n+1
    return out
def stick_for(dx,dz):
    R=cam(); rx,rz,fx,fz=R[0][0],R[0][2],R[2][0],R[2][2]; det=rx*fz-fx*rz
    sx,sy=(dx*fz-fx*dz)/det,(rx*dz-dx*rz)/det; l=math.hypot(sx,sy); return sx/l,sy/l
def axis(v): return max(0,min(255,int(128+v*127)))
log=[]
def act():
    time.sleep(0.3)
    s=spike(); tgt=min(monkeys(), key=lambda o:(lambda m: math.hypot(m[0]-s[0],m[2]-s[2]))(struct.unpack("<3h",rd(o+0x1C,6))))
    for _ in range(120):
        s=spike(); m=struct.unpack("<3h",rd(tgt+0x1C,6)); d=math.hypot(m[0]-s[0],m[2]-s[2])
        sx,sy=stick_for(m[0]-s[0],m[2]-s[2])
        if d<260:
            call("clear_input"); time.sleep(0.05)
            call("set_input",buttons=0xFFFF,lx=0x80,ly=0x80,rx=axis(sx),ry=axis(-sy)); time.sleep(0.08); call("clear_input")
            log.append(("swing at",round(d))); time.sleep(0.6)
            if tgt not in monkeys(): log.append("caught"); return
            continue
        call("set_input",buttons=0xFFFF,lx=axis(sx),ly=axis(-sy),rx=0x80,ry=0x80); time.sleep(0.08)
    call("clear_input")
def setup():
    call("clear_input"); call("savestate",op="load",slot=1); time.sleep(1.0); call("spu_events_reset")
    threading.Thread(target=act).start()
evs=[]
setup()
seen=set(); shots=[]; t0=time.time(); i=0
while time.time()-t0<17:
    r=call("spu_events",count=4096)["events"]
    new=[e["addr"] for e in r if e["kind"]=="KEYON" and e["seq"] not in seen and int(e["addr"],16) not in (0x56500,0x2D300,0x0BA90,0x13360,0x0D030)]
    for e in r: seen.add(e["seq"])
    if 2<time.time()-t0<16:
        p="D:/Ape Escape Recomp/modding/shots/oc%d.png"%i; call("screenshot_hires",path=p); shots.append((round(time.time()-t0,1),new,p)); i+=1
    elif new: shots.append((round(time.time()-t0,1),new,None))
    time.sleep(0.4)
for s_ in shots: print(s_[0], s_[1])
from PIL import Image
ims=[x for x in shots if x[2]]
out=Image.new("RGB",(8*192,((len(ims)+7)//8)*120))
for k,(t,n,p) in enumerate(ims):
    out.paste(Image.open(p).convert("RGB").resize((192,120)),((k%8)*192,(k//8)*120))
out.save("D:/Ape Escape Recomp/modding/shots/oc_grid.png")
print(log)
for e in []:
    if int(e["addr"],16) not in MUSIC: print(e["frame"], e["v"], e["addr"], e["pitch"])
