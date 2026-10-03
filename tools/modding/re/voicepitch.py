"""Voice pitch (f0) of the XA clips in the warp-in voice interleave, by
autocorrelation on 40 ms frames; compares unknown clips with Spike's."""
import math
from xadec import extract
RATE = 18900
def f0_track(x):
    out = []
    n = int(RATE * 0.04); hop = n // 2
    lo, hi = int(RATE / 600), int(RATE / 80)
    for s in range(0, len(x) - n - hi, hop):
        w = x[s:s + n + hi]
        e = sum(v * v for v in w[:n])
        if e / n < 300 ** 2:                       # skip quiet frames
            continue
        best, lag_best = 0, 0
        for lag in range(lo, hi):
            c = sum(w[i] * w[i + lag] for i in range(0, n, 2))
            if c > best: best, lag_best = c, lag
        if lag_best and best / (e / 2) > 0.45:     # voiced
            out.append(RATE / lag_best)
    return out
def median(v): v = sorted(v); return v[len(v) // 2] if v else 0
for ch in (0, 1, 2, 3, 8, 9, 10, 11):
    L, R, lbas = extract(51083, 1, ch)
    tr = f0_track(L)
    q = sorted(tr)
    p10, p90 = (q[len(q) // 10], q[9 * len(q) // 10]) if q else (0, 0)
    print("ch %2d  %.2fs  voiced frames %3d  f0 median %4.0f Hz  (10-90%%: %4.0f-%4.0f)" %
          (ch, len(L) / RATE, len(tr), median(tr), p10, p90))
