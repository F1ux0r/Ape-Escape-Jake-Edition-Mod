import struct, sys
from dbg import call
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
def w(a): return struct.unpack("<I",rd(a,4))[0]
idx=int(sys.argv[1]) if len(sys.argv)>1 else 0
OT=0x800EC3D0+0x4078*idx
a=w(OT+0x4000)
print("head",hex(a))
n=0
seen=set()
while n<400:
    addr=0x80000000|(a&0xFFFFFF)
    if (a&0xFFFFFF)==0xFFFFFF or addr in seen: break
    seen.add(addr)
    hdr=w(addr); ln=hdr>>24
    words=struct.unpack("<%dI"%ln, rd(addr+4,4*ln)) if ln else ()
    if ln:
        op=words[0]>>24
        print(hex(addr), ln, " ".join("%08X"%x for x in words))
    a=hdr&0xFFFFFF; n+=1
