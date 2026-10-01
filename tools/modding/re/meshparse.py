import struct, collections
B=0x80000000
SIZES={0x24:0x1C,0x2C:0x1C,0x34:0x1C,0x3C:0x1C,0x20:0x10,0x28:0x10,0x30:0x10,0x38:0x10}
def parse(r, polys, npolys):
    """Yield (addr, code, rec bytes) for each poly."""
    a=polys
    for i in range(npolys):
        b=r(a,4); code=b[3]&0xFC
        if code not in SIZES: raise ValueError(f"bad code {code:#x} at {a:#x} (poly {i})")
        yield a, code, r(a, SIZES[code])
        a+=SIZES[code]
def textures(r, polys, npolys):
    info=collections.defaultdict(lambda:[999,999,-1,-1,0])
    for a,code,rec in parse(r,polys,npolys):
        if code not in (0x24,0x2C,0x34,0x3C): continue
        nv=3 if code in (0x24,0x34) else 4
        u0,v0,clut=rec[12],rec[13],struct.unpack_from("<H",rec,14)[0]
        u1,v1,tpage=rec[16],rec[17],struct.unpack_from("<H",rec,18)[0]
        uv=[(u0,v0),(u1,v1),(rec[20],rec[21])]+([(rec[22],rec[23])] if nv==4 else [])
        k=info[(tpage,clut)]
        for u,v in uv: k[0]=min(k[0],u);k[1]=min(k[1],v);k[2]=max(k[2],u);k[3]=max(k[3],v)
        k[4]+=1
    return dict(info)
