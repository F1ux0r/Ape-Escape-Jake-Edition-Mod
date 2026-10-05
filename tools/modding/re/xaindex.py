"""Index every XA audio stream on the disc: (file, channel) runs that end on the
EOF submode bit. Prints coding (mono/stereo, rate) and length per stream."""
import json, sys
BIN = "D:/Ape Escape (USA)/Ape Escape (USA).bin"
streams = {}   # (file, chan) -> current open stream
done = []
with open(BIN, "rb") as f:
    lba = 0
    while True:
        raw = f.read(2352)
        if len(raw) < 2352: break
        sh = raw[16:20]
        if raw[15] == 2 and sh[2] & 0x04 and sh == raw[20:24]:
            k = (sh[0], sh[1])
            s = streams.get(k)
            if s is None:
                s = streams[k] = {"file": sh[0], "chan": sh[1], "coding": sh[3], "first": lba, "lbas": []}
            s["lbas"].append(lba)
            if sh[2] & 0x80:
                s["last"] = lba; s["n"] = len(s["lbas"]); done.append(s); del streams[k]
        lba += 1
for s in streams.values():
    s["last"] = s["lbas"][-1]; s["n"] = len(s["lbas"]); done.append(s)
for s in done:
    c = s["coding"]; stereo = c & 1; rate = 18900 if c & 4 else 37800
    spf = 4032 // (2 if stereo else 1)   # samples per channel per sector (4-bit)
    s["stereo"] = stereo; s["rate"] = rate; s["secs"] = s["n"] * spf / rate
json.dump(done, open(sys.argv[1], "w"))
from collections import Counter
print("streams", len(done), Counter((s["stereo"], s["rate"]) for s in done))
