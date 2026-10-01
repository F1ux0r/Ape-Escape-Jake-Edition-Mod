import time, struct
from dbg import call
from nettest import arm_net
D=0x9F000000
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def w32(a,v):
    for i in range(4): call("write_ram",addr=a+i,val=(v>>(8*i))&0xFF)
def probe(tag):
    w32(D+184,0); time.sleep(0.025)
    pk,en,w0=struct.unpack("<3I",rd(D+184,12))
    if not pk: print(tag,"no emit"); return
    t=struct.unpack("<3I",rd(pk,12)); e=struct.unpack("<I",rd(en,4))[0]
    ph=struct.unpack("<i",rd(D+4,4))[0]
    # walk chain from entry a few links to see if packet reachable
    a=e&0xFFFFFF; hit=False
    for _ in range(400):
        if (a|0x80000000)==pk: hit=True;break
        if a==0xFFFFFF: break
        a=struct.unpack("<I",rd(a|0x80000000,4))[0]&0xFFFFFF
    print(tag,"ph",ph,"pk",hex(pk),"tag",hex(t[0]),"w0",hex(t[1]),"expect",hex(w0),"entry",hex(e),"reachable",hit)
print(arm_net()); time.sleep(0.5)
probe("idle"); probe("idle")
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=0x80, ry=0x00); time.sleep(0.07); call("clear_input")
for i in range(8): probe(f"t{i}")
