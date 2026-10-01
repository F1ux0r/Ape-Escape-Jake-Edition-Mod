import time
from dbg import call
from PIL import Image
S="D:/Ape Escape Recomp/modding/shots/"
call("clear_input"); call("savestate",op="load",slot=10); time.sleep(0.1); call("spu_events_reset")
seen=set(); rows=[]; t0=time.time()
for i in range(28):
    call("screenshot_hires",path=f"{S}rc{i}.png")
    r=call("spu_events",count=4096)
    new=[e for e in r["events"] if e["kind"]=="KEYON" and e["seq"] not in seen and int(e["addr"],16) not in (0x425C0,0x54D40,0x55330,0x4C290,0x4DA60,0x520B0,0x52E80)]
    for e in r["events"]: seen.add(e["seq"])
    rows.append((i,round(time.time()-t0,2),[e["addr"] for e in new]))
    time.sleep(0.15)
for r in rows: print(r)
out=Image.new("RGB",(7*256,4*160))
for i in range(28):
    out.paste(Image.open(f"{S}rc{i}.png").convert("RGB").resize((256,160)),((i%7)*256,(i//7)*160))
out.save(f"{S}rc_grid.png")
