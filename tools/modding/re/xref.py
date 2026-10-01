# Find code that touches given guest addresses (lui+offset pairs) in SCUS_944.23.
import sys, capstone
data = open("re/SCUS_944.23", "rb").read()
BASE = 0x80010000; code = data[0x800:]
md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
md.detail = True
md.skipdata = True
insns = list(md.disasm(code, BASE))
open("re/disasm.txt", "w").write("\n".join(f"{i.address:08X}  {i.mnemonic:8} {i.op_str}" for i in insns))
targets = {int(a, 16): a for a in sys.argv[1:]}
lui = {}
for i in insns:
    if i.id == 0: continue
    ops = i.operands
    if i.mnemonic == "lui":
        lui[ops[0].reg] = (ops[1].imm << 16, i.address)
    elif i.mnemonic in ("addiu",) and len(ops) == 3 and ops[1].reg in lui:
        v = (lui[ops[1].reg][0] + ops[2].imm) & 0xFFFFFFFF
        if v in targets: print(f"{i.address:08X} addr-of {targets[v]}  ({i.mnemonic} {i.op_str})")
    elif len(ops) == 2 and ops[1].type == capstone.mips.MIPS_OP_MEM and ops[1].mem.base in lui:
        base, at = lui[ops[1].mem.base]
        if i.address - at > 64: continue
        v = (base + ops[1].mem.disp) & 0xFFFFFFFF
        for t, name in targets.items():
            if t <= v < t + 4 and (v == t or t & 3 == 0 and False) or v == t:
                print(f"{i.address:08X} {i.mnemonic:4} {name}  ({i.op_str})")
    if i.mnemonic in ("jr", "jal", "j") and False: pass
