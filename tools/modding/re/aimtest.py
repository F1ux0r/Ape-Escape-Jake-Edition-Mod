import sys, time, struct; sys.path.insert(0, "re")
from dbg import call
from tap import tap
def rd(a, n): return bytes.fromhex(call("read_ram", addr=a, len=n)["hex"])
from throwtest_lib import arm
MONKEY_OBJ, MONKEY_COL = 0x800E6B28, 0x800E6B4C
rx, ry = int(sys.argv[1], 0), int(sys.argv[2], 0)
arm()
m0 = rd(MONKEY_OBJ, 0x100); mpos = struct.unpack("<3i", rd(MONKEY_COL, 12))
call("wtrace_clear"); call("wtrace_range", lo=0x800BD1A0, hi=0x800BD1A0 + 0x38*128)
call("set_input", buttons=0xFFFF, lx=0x80, ly=0x80, rx=rx, ry=ry); time.sleep(0.06); call("clear_input")
time.sleep(0.5); call("screenshot_hires", path="D:/Ape Escape Recomp/modding/shots/aim_mid.png")
time.sleep(1.3)
m1 = rd(MONKEY_OBJ, 0x100)
r = call("wtrace_dump", addr_lo=0x800BD1A0, addr_hi=0x800BD1A0 + 0x38*128, count=20000)
pos = {}
for e in r["entries"]:
    if e["ra"] != "0x80063DAC": continue
    a = int(e["addr"], 16) | 0x80000000; off = (a - 0x800BD1A0) % 0x38; key = (e["frame"], a - off)
    if off in (0, 4, 8): pos.setdefault(key, {})[off] = struct.unpack("<i", struct.pack("<I", int(e["new"], 16)))[0]
pts = [(v[0], v[4], v[8]) for v in pos.values() if len(v) == 3]
if pts:
    best = min(pts, key=lambda p: sum((p[i]-mpos[i])**2 for i in range(3)))
    print("monkey", mpos, "club samples", len(pts), "closest", best, "dist %.0f" % sum((best[i]-mpos[i])**2 for i in range(3))**0.5)
print("monkey struct changed offsets:", [hex(i) for i in range(0x100) if m0[i] != m1[i]][:40])
