"""Two 36x32 Jake portraits for the title-screen toggle (our own art): the
HUD face icon doubled, with hand-drawn eyes - normal (blue hair, big brown
eyes) and evil (purple hair, red slanted eyes, angry brows). 4-bit, index 0
transparent; both use the same indices with their own palette."""
import sys
from PIL import Image
rows = [l.strip() for l in open("face_new.txt") if l.strip()]
W, H = 36, 32
grid = [[int(rows[y // 2][x // 2], 16) for x in range(W)] for y in range(H)]
SKIN, SKIN_D, WHITE, OUT, IRIS, IRIS_D, BROW = 11, 7, 15, 2, 12, 9, 2
def put(x, y, k):
    if 0 <= x < W and 0 <= y < H: grid[y][x] = k
# clear the doubled eye/brow rows to skin first (cols 8..29, rows 16..23)
for y in range(16, 28):
    for x in range(6, 32):
        if grid[y][x] not in (0, 3, 4, 5, 14, 2) or (y >= 18 and grid[y][x] == 2 and 8 < x < 28):
            grid[y][x] = SKIN
for x in range(16, 21): grid[27][x] = 9          # mouth
EYE = [".oooooo.",
       "oowiiwoo",
       "wwiIiiww",
       "wwiiiiww",
       "wwiiiiww",
       ".wwiiww.",
       "..wwww.."]
EVIL = ["o.......",
        "ooo.....",
        ".owooo..",
        ".owwwwoo",
        ".owRRwwo",
        "..oRRwwo",
        "...oooo."]
def draw(art, x0, y0, flip, keys):
    for j, row in enumerate(art):
        for i, ch in enumerate(row[::-1] if flip else row):
            if ch != ".": put(x0 + i, y0 + j, keys[ch])
def normal_eyes():
    keys = {"o": OUT, "w": WHITE, "i": IRIS, "I": WHITE}
    draw(EYE, 9, 18, False, keys); draw(EYE, 19, 18, False, keys)
    for x in range(10, 16): put(x, 15, BROW)
    for x in range(20, 26): put(x, 15, BROW)
def evil_eyes():
    keys = {"o": OUT, "w": WHITE, "R": 12}
    draw(EVIL, 9, 17, True, keys); draw(EVIL, 19, 17, False, keys)
PAL_COMMON = {0: 0x0000, 1: 0xBDF0, 2: 0x94C5, 6: 0x801F, 7: 0xBA7D, 8: 0xE739, 9: 0x8D0F,
              10: 0xB1F8, 11: 0xC6BE, 12: 0x9116, 13: 0xD6B5, 15: 0xFFFF}
def rgb15(r, g, b): return 0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
HAIR = {"evil": [(64, 24, 120), (104, 48, 176), (136, 72, 208), (144, 120, 240)],
        "normal": [(16, 56, 136), (32, 96, 200), (64, 136, 232), (120, 184, 248)]}
def build(kind):
    g0 = [r[:] for r in grid]
    (normal_eyes if kind == "normal" else evil_eyes)()
    out = [r[:] for r in grid]
    for y in range(H): grid[y][:] = g0[y]
    pal = [0] * 16
    for k, v in PAL_COMMON.items(): pal[k] = v
    for k, c in zip((3, 4, 5, 14), HAIR[kind]): pal[k] = rgb15(*c)
    pal[12] = rgb15(120, 72, 32) if kind == "normal" else rgb15(232, 24, 24)
    return out, pal
if __name__ == "__main__":
    prev = Image.new("RGB", (W * 2 + 8, H), (40, 120, 40))
    res = {}
    for n, kind in enumerate(("normal", "evil")):
        px, pal = build(kind); res[kind] = (px, pal)
        for y in range(H):
            for x in range(W):
                c = pal[px[y][x]]
                if px[y][x]: prev.putpixel((n * (W + 8) + x, y), ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3))
    prev.resize((prev.width * 6, prev.height * 6), Image.NEAREST).save(sys.argv[1])
