"""Render 'JAKE EDITION' in the style of the Ape Escape title logo:
brush caps with a per-letter tilt and bounce, yellow-orange fill with a
pale rim, navy outline and a blue extrusion down-right."""
import sys, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

SS = 4                      # supersample
DX = 3                      # extrusion: DX/4 px left per px down

def letter_mask(ch, font, angle):
    f = font
    l, t, r, b = f.getbbox(ch)
    m = Image.new("L", (r - l + 40 * SS, b - t + 40 * SS), 0)
    ImageDraw.Draw(m).text((20 * SS - l, 20 * SS - t), ch, font=f, fill=255)
    m = m.rotate(angle, resample=Image.BICUBIC, expand=True)
    return m.crop(m.getbbox())

def layout(text, font, tilt, bounce, gap, space):
    pieces, x = [], 0
    for i, ch in enumerate(text):
        if ch == " ":
            x += space; continue
        a = tilt[i % len(tilt)]
        m = letter_mask(ch, font, a)
        pieces.append((x, bounce[i % len(bounce)], m))
        x += m.width + gap
    w = x - gap
    h = max(dy + m.height for _, dy, m in pieces) - min(dy for _, dy, m in pieces)
    y0 = -min(dy for _, dy, m in pieces)
    face = Image.new("L", (w, h), 0)
    for px, dy, m in pieces:
        face.paste(255, (px, dy + y0), m)
    return face

def dilate(m, r):
    return m.filter(ImageFilter.MaxFilter(2 * r + 1)) if r > 0 else m

def style(face, outline, depth, rim):
    pad = outline + depth + 4 * SS
    W, H = face.width + 2 * pad, face.height + 2 * pad
    f = Image.new("L", (W, H), 0); f.paste(face, (pad, pad))
    out = dilate(f, outline)
    # extrusion: outline mask shifted down-right step by step
    ext = Image.new("L", (W, H), 0)
    for s in range(1, depth + 1):
        ext = ImageChops.lighter(ext, ImageChops.offset(out, -(s * DX) // 4, s))
    ext_out = dilate(ext, max(1, SS // 2))
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    def put(mask, col): img.paste(Image.new("RGBA", (W, H), col), (0, 0), mask)
    put(ext_out, (8, 16, 72, 255))                       # extrusion outline
    # extrusion shading: lighter blue near the face
    for s in range(depth, 0, -1):
        t = s / depth
        col = (int(30 + 40 * (1 - t)), int(70 + 90 * (1 - t)), int(190 + 50 * (1 - t)), 255)
        put(ImageChops.offset(dilate(f, max(0, outline - SS)), -(s * DX) // 4, s), col)
    put(out, (10, 20, 90, 255))                          # navy outline
    inner = dilate(f, rim)
    put(inner, (255, 250, 225, 255))                     # pale rim
    # face fill: yellow top -> orange bottom, with brush streaks
    grad = Image.new("RGBA", (W, H))
    gd = ImageDraw.Draw(grad)
    bb = f.getbbox()
    for y in range(H):
        t = min(1, max(0, (y - bb[1]) / max(1, bb[3] - bb[1])))
        r, g, b = 255, int(225 - 95 * t), int(60 - 50 * t)
        gd.line([(0, y), (W, y)], fill=(r, g, max(0, b), 255))
    for k in range(0, W, 9 * SS):                        # diagonal streaks
        gd.line([(k, bb[3]), (k + (bb[3] - bb[1]) // 2, bb[1])], fill=(250, 140, 20, 255), width=SS)
    shrink = f.filter(ImageFilter.MinFilter(2 * SS + 1))
    img.paste(grad, (0, 0), shrink)
    return img.crop(img.getbbox())

def render(font_path, size, text="JAKE EDITION", tilt=(-8, 6, -4, 9, 0, -6, 7, -3, 5, -7, 4, -5),
           bounce=(0, -4, 3, -2, 0, 4, -3, 2, -4, 3, -2, 1)):
    font = ImageFont.truetype(font_path, size * SS)
    face = layout(text, font, tilt, [b * SS for b in bounce], gap=-1 * SS, space=size * SS // 3)
    return style(face, outline=2 * SS, depth=3 * SS, rim=1 * SS)

if __name__ == "__main__":
    out = sys.argv[1]
    rows = []
    for fp in sys.argv[2:]:
        im = render(fp, 40)
        im = im.resize((im.width // SS, im.height // SS), Image.LANCZOS)
        rows.append((fp, im))
    W = max(im.width for _, im in rows) + 20
    sheet = Image.new("RGB", (W, sum(im.height + 16 for _, im in rows)), (40, 120, 40))
    y = 0
    for fp, im in rows:
        sheet.paste(im, (10, y + 8), im); y += im.height + 16
    sheet.save(out)
