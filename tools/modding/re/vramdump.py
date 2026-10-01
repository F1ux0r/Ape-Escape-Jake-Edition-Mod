import sys, time
from dbg import call
from PIL import Image
def dump(path):
    im=Image.new("RGB",(1024,512))
    px=im.load()
    for y0 in range(0,512,128):
        for x0 in range(0,1024,128):
            r=call("vram_peek",x=x0,y=y0,w=128,h=128); h=r["hex"]
            for i in range(128*128):
                v=int(h[i*4:i*4+4],16); x=x0+i%128; y=y0+i//128
                px[x,y]=((v&31)<<3,((v>>5)&31)<<3,((v>>10)&31)<<3)
    im.save(path)
if __name__=="__main__":
    call("savestate",op="load",slot=int(sys.argv[1])); time.sleep(1.5)
    dump(sys.argv[2])
