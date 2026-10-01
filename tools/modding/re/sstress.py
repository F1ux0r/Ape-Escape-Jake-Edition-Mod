import time
from dbg import call
bad=0
for i in range(20):
    slot=[9,10,1][i%3]
    call("spu_events_reset")
    call("savestate",op="load",slot=slot); time.sleep(1.2)
    f0=call("status").get("frame") if False else None
    hb=call("spu_events",count=4096)["events"]
    irq=[e for e in hb if e["kind"]=="IRQ"]
    st=call("spu_status")
    jake=call("spu_ram",addr=0x38F00,len=32)["hex"][32:36]
    a=call("read_ram",addr=0x800EC1E0,len=4)["hex"]
    time.sleep(0.3)
    print(i, "slot",slot, "irq events",len(irq), "irq_flag",st["irq_flag"], "ctrl",st["ctrl"], "irq_addr",st["irq_addr"], "djump slot hdr",jake)
