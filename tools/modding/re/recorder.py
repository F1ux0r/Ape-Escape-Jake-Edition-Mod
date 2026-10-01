import time, struct, json
from dbg import call
MUSIC={0x425C0,0x54D40,0x55330,0x4C290,0x4DA60,0x520B0,0x52E80,0x48F60,0x49C80,0x4BDE0,0x4A880,0x55820,0x41810,0x432D0,0x44340,0x54BD0,0x45670,0x57EE0,0x56500}
seen=set(); t0=time.time(); out=open("recorder_log.txt","w")
call("spu_events_reset")
while time.time()-t0<240:
    try:
        st=struct.unpack("<H",bytes.fromhex(call("read_ram",addr=0x800EC1E0+0x5E,len=2)["hex"]))[0]
        hp=call("read_ram",addr=0x800EC1E0,len=1)
        r=call("spu_events",count=4096)
    except Exception as e:
        time.sleep(0.5); continue
    for e in r["events"]:
        if e["kind"]!="KEYON" or e["seq"] in seen: continue
        seen.add(e["seq"])
        if int(e["addr"],16) in MUSIC: continue
        out.write("%.2f st=%02X %s %s\n"%(time.time()-t0, st, e["addr"], e["pitch"])); out.flush()
    time.sleep(0.15)
