import time, sys
from dbg import call
from PIL import Image
name=sys.argv[1] if len(sys.argv)>1 else "tail"
S="D:/Ape Escape Recomp/modding/shots/"
call("savestate",op="load",slot=9); time.sleep(1.5)
X=0x4000
seq=[("run",dict(buttons=0xFFFF,lx=0x00,ly=0x80),0.0),
     ("run",None,0.25),("run",None,0.25),("run",None,0.25),
     ("stop",dict(buttons=0xFFFF,lx=0x80,ly=0x80),0.06),("stop",None,0.06),("stop",None,0.06),("stop",None,0.06),
     ("turn",dict(buttons=0xFFFF,lx=0xFF,ly=0x80),0.06),("turn",None,0.06),("turn",None,0.06),("turn",None,0.1),
     ("jump",dict(buttons=0xFFFF&~X,lx=0x80,ly=0x80),0.08),("jump",None,0.1),("jump",None,0.1),("jump",None,0.15)]
shots=[]
for i,(lab,inp,dt) in enumerate(seq):
    if inp: call("set_input",rx=0x80,ry=0x80,**inp)
    time.sleep(dt)
    p=f"{S}{name}{i}.png"; call("screenshot_hires",path=p); shots.append((lab,p))
call("clear_input")
W,H=4,4
out=Image.new("RGB",(W*288,H*216))
for i,(lab,p) in enumerate(shots):
    im=Image.open(p).convert("RGB").crop((120,50,264,158)).resize((288,216),Image.NEAREST)
    out.paste(im,((i%W)*288,(i//W)*216))
out.save(f"{S}{name}_grid.png")
