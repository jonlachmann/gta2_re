# Annotates captures with the HUD's element zones (from Hud.cpp positions, 640x480 HUD space) and the minimap slot.
# usage: python3 -I placement.py <captures dir> <out dir>
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw
from mm_lib import font
CAPS, OUT = sys.argv[1:3]
F = font(True, 11)
FT = font(True, 15)
# (label, x0, y0, x1, y1, anchor) in 1x HUD px; anchor l/r/c says how the port places it on wide screens
ZONES = [
    ('respect meters', 0, 0, 104, 92, 'l'),
    ('pagers (timers)', 3, 104, 59, 200, 'l'),
    ('wanted heads', 260, 0, 380, 18, 'c'),
    ('zone / car name', 210, 20, 430, 50, 'c'),
    ('lives x mult', 495, 0, 530, 42, 'c'),
    ('clock (port)', 430, 4, 486, 26, 'c'),
    ('score, hearts', 540, 0, 640, 42, 'r'),
    ('weapon, ammo', 548, 44, 640, 96, 'r'),
    ('brief: portrait + text', 0, 370, 640, 480, 'l'),
]
MM = (96 + 6, 101 + 6)  # minimap A plus margin


def annotate(cap, s, title):
    im = Image.open(os.path.join(CAPS, cap)).convert('RGBA'); W, H = im.size
    ov = Image.new('RGBA', im.size, (0, 0, 0, 0)); d = ImageDraw.Draw(ov)

    def tx(x, a):
        if a == 'l':
            return x * s
        if a == 'r':
            return W - (640 - x) * s
        return W / 2 + (x - 320) * s
    for name, x0, y0, x1, y1, a in ZONES:
        if name.startswith('brief'):
            X0, X1 = 0, min(W, 640 * s)
        else:
            X0, X1 = tx(x0, a), tx(x1, a)
        Y0, Y1 = y0 * s, min(H, y1 * s)
        d.rectangle((X0, Y0, X1 - 1, Y1 - 1), fill=(60, 140, 255, 50), outline=(120, 190, 255, 255), width=2)
        d.text((X0 + 3, Y1 - 14 if Y1 - Y0 > 20 else Y1 + 2), name, font=F, fill=(200, 230, 255, 255))
    mx0, my0 = W - MM[0] * s, H - MM[1] * s
    d.rectangle((mx0, my0, W - 6 * s, H - 6 * s), fill=(255, 200, 40, 70), outline=(255, 210, 60, 255), width=3)
    d.text((mx0 + 4, my0 + 4), 'MINIMAP', font=FT, fill=(255, 230, 120, 255))
    im.alpha_composite(ov)
    out = Image.new('RGB', (W, H + 24), (24, 24, 26)); out.paste(im.convert('RGB'), (0, 24))
    ImageDraw.Draw(out).text((6, 4), title, font=FT, fill=(235, 220, 160))
    return out


a = annotate('cap_day43/gl_00600.png', 1.0, '640x480: the minimap overlaps the last brief lines (bottom right)')
b = annotate('cap_day169/gl_00600.png', 1.5, '1280x720 (HUD 1.5x): the brief stays 4:3 wide, the corner is free')
b = b.resize((b.width * 2 // 3, b.height * 2 // 3), Image.BOX)
out = Image.new('RGB', (a.width + b.width + 12, max(a.height, b.height)), (24, 24, 26))
out.paste(a, (0, 0)); out.paste(b, (a.width + 12, 0))
out.save(os.path.join(OUT, 'placement.png'))
