import sys, struct, wave
BIN="D:/Ape Escape (USA)/Ape Escape (USA).bin"
K=[(0,0),(60,0),(115,-52),(98,-55)]
def sector(lba):
    with open(BIN,"rb") as f:
        f.seek(lba*2352); return f.read(2352)
def decode_sector(raw, st):
    data=raw[24:24+2304]; outL=[];outR=[]
    for g in range(18):
        grp=data[g*128:(g+1)*128]
        for blk in range(8):
            hdr=grp[4+blk]; sh=hdr&15; flt=(hdr>>4)&3
            k0,k1=K[flt]; ch=blk&1
            o1,o2=st[ch]
            res=[]
            for i in range(28):
                b=grp[16+i*4+blk//2]
                n=(b>>((blk&1)*4))&15
                if n>=8:n-=16
                s=(n<<12)>>sh
                s=s+((o1*k0+o2*k1+32)>>6)
                s=max(-32768,min(32767,s)); res.append(s); o2=o1;o1=s
            st[ch]=(o1,o2)
            (outL if ch==0 else outR).extend(res)
    return outL,outR
def extract(start, file_, chan, maxsec=4000, out=None):
    st=[(0,0),(0,0)]; L=[];R=[]; lba=start; n=0; lbas=[]
    while n<maxsec:
        raw=sector(lba)
        sh=raw[16:20]
        if sh[0]==file_ and sh[1]==chan and sh[2]&0x04:
            l,r=decode_sector(raw,st); L+=l; R+=r; lbas.append(lba)
            if sh[2]&0x80: break
        lba+=1; n+=1
    if out:
        w=wave.open(out,"wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(37800)
        w.writeframes(b"".join(struct.pack("<hh",a,b) for a,b in zip(L,R))); w.close()
    return L,R,lbas
if __name__=="__main__":
    L,R,lbas=extract(int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3]),out=sys.argv[4])
    print("sectors",len(lbas),"lba",lbas[0],lbas[-1],"secs %.2f"%(len(L)/37800))
    w=int(37800*0.1)
    env=[int((sum(x*x for x in L[i:i+w])/w)**0.5) for i in range(0,len(L)-w,w)]
    pk=max(env); print("".join(" .:-=+*#%@"[min(9,int(9*e/pk))] for e in env))
