import sys, wave, struct
from dbg import call
F=[(0,0),(60,0),(115,-52),(98,-55),(122,-60)]
def ram(a,n):
    r=call("spu_ram",addr=a,len=n); return bytes.fromhex(r["hex"])
def extract(addr, maxlen=0x20000):
    """raw ADPCM blocks from addr through the block with the end flag"""
    out=b""; a=addr
    while len(out)<maxlen:
        blk=ram(a,0x800); 
        for i in range(0,len(blk),16):
            b=blk[i:i+16]; out+=b
            if b[1]&1: return out
        a+=0x800
    return out
def decode(adpcm):
    s1=s2=0; pcm=[]
    for i in range(0,len(adpcm),16):
        b=adpcm[i:i+16]; sh=b[0]&15; f=(b[0]>>4)&7
        f0,f1=F[min(f,4)]
        for j in range(28):
            n=(b[2+j//2]>>((j&1)*4))&15
            if n>=8: n-=16
            s=(n<<12)>>sh
            s+= (s1*f0 + s2*f1 + 32)>>6
            s=max(-32768,min(32767,s)); pcm.append(s); s2=s1; s1=s
    return pcm
if __name__=="__main__":
    addr=int(sys.argv[1],0); pitch=int(sys.argv[2],0); out=sys.argv[3]
    raw=extract(addr); pcm=decode(raw)
    rate=int(44100*pitch/4096)
    with wave.open(out,"wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(struct.pack("<%dh"%len(pcm),*pcm))
    print(hex(addr),"bytes",hex(len(raw)),"secs %.2f"%(len(pcm)/rate),"rate",rate)
    open(out.replace(".wav",".adpcm"),"wb").write(raw)
