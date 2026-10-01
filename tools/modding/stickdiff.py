import time, struct
from dbg import call
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def dump():
    return b"".join(rd(0x80000000+o,0x40000) for o in range(0,0x200000,0x40000))
SQ=0x8000
call("savestate",op="load",slot=9); time.sleep(1.5)
for i,b in enumerate(bytes.fromhex("0003060100ffff01")): call("write_ram",addr=0x800F51A8+i,val=b)
call("set_input", buttons=0xFFFF & ~SQ, lx=0x80, ly=0x80, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.6)
res={}
for name,(rx,ry) in {"L":(0x00,0x80),"R":(0xFF,0x80),"L2":(0x00,0x80),"U":(0x80,0x00)}.items():
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=rx, ry=ry); time.sleep(0.8)
    res[name]=dump()
    print(name, hex(struct.unpack("<H",res[name][0xEC1E0+0x5E:0xEC1E0+0x60])[0]))
call("clear_input")
L,R,L2,U=res["L"],res["R"],res["L2"],res["U"]
hits=[i for i in range(len(L)) if L[i]==L2[i] and L[i]!=R[i]]
print(len(hits))
for i in hits[:300]:
    print(hex(0x80000000+i), L[i], R[i], U[i])
