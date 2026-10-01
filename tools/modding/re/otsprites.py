import struct, sys
from dbg import call
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
mem=rd(0x80000000,0x200000) if False else b"".join(rd(0x80000000+o,0x40000) for o in range(0,0x200000,0x40000))
def w(a): return struct.unpack_from("<I",mem,a&0x1FFFFF)[0]
for idx in (0,1):
    OT=0x800EC3D0+0x4078*idx
    for start in (OT+0x3FFC, OT+0x4000):
        a=w(start)&0xFFFFFF; n=0; seen=set()
        while a!=0xFFFFFF and n<20000 and a not in seen:
            seen.add(a); hdr=w(a); ln=hdr>>24
            ws=[w(a+4+4*k) for k in range(ln)]
            for k in range(ln):
                op=ws[k]>>24
                if 0x64<=op<=0x67 and k+3<ln:
                    yx=ws[k+1]; x,y=yx&0xFFFF,yx>>16
                    if x>=250 and y>=150:
                        tp=ws[k-1]&0x1FF if k>0 and ws[k-1]>>24==0xE1 else -1
                        uv=ws[k+2]; wh=ws[k+3]
                        print(idx,hex(start-OT),hex(0x80000000|a),"tp %X x=%d y=%d uv=%04X clut=%04X %dx%d"%(tp,x,y,uv&0xFFFF,uv>>16,wh&0xFFFF,wh>>16))
            a=hdr&0xFFFFFF; n+=1
