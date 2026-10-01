import time
from dbg import call
def rd(a): return bytes.fromhex(call("read_ram",addr=a,len=1)["hex"])[0]
SQ,R3=0x8000,0x0004
def press(b):
    call("set_input", buttons=0xFFFF & ~b, lx=0x80, ly=0x80, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.8)
call("savestate",op="load",slot=9); time.sleep(1.5)
for i,b in enumerate(bytes.fromhex("0003060100ffff01")): call("write_ram",addr=0x800F51A8+i,val=b)
press(SQ); print("equipped", rd(0x800F51C0))
press(R3); print("R3 (unpowered)", rd(0x800F51C0))
press(SQ); print("powered on", rd(0x800F51C0))
press(R3); print("R3 (powered)", rd(0x800F51C0))
press(R3); print("R3 (powered)", rd(0x800F51C0))
call("screenshot_hires",path="D:/Ape Escape Recomp/modding/shots/ammo_powered.png")
press(SQ); time.sleep(0.5); print("powered off", rd(0x800F51C0))
call("screenshot_hires",path="D:/Ape Escape Recomp/modding/shots/ammo_off.png")
