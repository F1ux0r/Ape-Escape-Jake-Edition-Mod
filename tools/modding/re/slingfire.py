import time, sys
from dbg import call
SQ=0x8000
presses=int(sys.argv[1]) if len(sys.argv)>1 else 1
call("savestate",op="load",slot=9); time.sleep(1.5)
for i,b in enumerate(bytes.fromhex("0003060100ffff01")): call("write_ram",addr=0x800F51A8+i,val=b)
for _ in range(presses):
    call("set_input", buttons=0xFFFF & ~SQ, lx=0x80, ly=0x80, rx=0x80, ry=0x80); time.sleep(0.1); call("clear_input"); time.sleep(0.6)
print("mark")
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=0x80, ry=0x00); time.sleep(0.8)
call("clear_input"); time.sleep(1.0)
