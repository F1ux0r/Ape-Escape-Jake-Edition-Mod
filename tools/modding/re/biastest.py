import sys, time
from dbg import call
from nettest import arm_net
for bias in [int(x) for x in sys.argv[1:]]:
    call("savestate", op="load", slot=9); time.sleep(1.5)
    call("write_ram", addr=0x9F000000 + 144, val=bias & 0xFF)
    call("write_ram", addr=0x9F000000 + 145, val=(bias >> 8) & 0xFF)
    arm_net(); time.sleep(0.5)
    call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=0x80, ry=0x00)
    time.sleep(0.07); call("clear_input")
    for i, t in enumerate([0.25, 0.35, 0.45]):
        time.sleep(0.1 if i else 0.18)
        call("screenshot_hires", path=f"D:/Ape Escape Recomp/modding/shots/bias{bias}_{i}.png")
