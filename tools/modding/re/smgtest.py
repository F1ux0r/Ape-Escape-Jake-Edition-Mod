import sys, time, struct
from dbg import call
from PIL import Image
def rd(a,n): return bytes.fromhex(call("read_ram",addr=a,len=n)["hex"])
SQ=0x8000
name=sys.argv[1] if len(sys.argv)>1 else "smg"
rx,ry=(int(sys.argv[2],0),int(sys.argv[3],0)) if len(sys.argv)>3 else (0x00,0x80)
call("savestate",op="load",slot=9); time.sleep(1.5)
for i,b in enumerate(bytes.fromhex("0003060100ffff01")): call("write_ram",addr=0x800F51A8+i,val=b)
for _ in range(2):
    call("set_input", buttons=0xFFFF & ~SQ, lx=0x80, ly=0x80, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.6)
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=rx, ry=ry)
shots=[]
for i in range(6):
    time.sleep(0.12); call("screenshot_hires",path=f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png")
    shots.append(hex(struct.unpack("<H",rd(0x800EC1E0+0x5E,2))[0]))
call("clear_input"); time.sleep(0.3)
call("screenshot_hires",path=f"D:/Ape Escape Recomp/modding/shots/{name}6.png"); shots.append(hex(struct.unpack("<H",rd(0x800EC1E0+0x5E,2))[0]))
print("states",shots)
out=Image.new("RGB",(768*4,480*2))
for i in range(7): out.paste(Image.open(f"D:/Ape Escape Recomp/modding/shots/{name}{i}.png").convert("RGB").resize((768,480),Image.NEAREST),((i%4)*768,(i//4)*480))
out.resize((2000,625)).save(f"D:/Ape Escape Recomp/modding/shots/{name}_grid.png")
