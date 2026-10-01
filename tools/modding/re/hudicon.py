import sys
from dbg import call
from PIL import Image
def peek(x,y,w,h):
    hx=call("vram_peek",x=x,y=y,w=w,h=h)["hex"]; return [int(hx[i*4:i*4+4],16) for i in range(w*h)]
def rgb(v): return ((v&31)<<3,((v>>5)&31)<<3,((v>>10)&31)<<3)
def icon(tpage,u,v,clut,w,h,out,scale=12):
    px0=(tpage&15)*64; py0=((tpage>>4)&1)*256
    cx=(clut&0x3F)*16; cy=clut>>6
    pal=peek(cx,cy,16,1)
    raw=peek(px0+u//4,py0+v,(w+3)//4+1,h)
    rw=(w+3)//4+1
    im=Image.new("RGB",(w,h))
    for y in range(h):
        for x in range(w):
            word=raw[y*rw+x//4]; i=(word>>((x%4)*4))&15
            im.putpixel((x,y),rgb(pal[i]) if pal[i] else (255,0,255))
    im.resize((w*scale,h*scale),Image.NEAREST).save(out)
    print("pal"," ".join("%04X"%p for p in pal))
    for y in range(h):
        print("".join("%X"%((raw[y*rw+x//4]>>((x%4)*4))&15) for x in range(w)))
if __name__=="__main__":
    a=[int(s,0) for s in sys.argv[1:7]]
    icon(*a,sys.argv[7])
