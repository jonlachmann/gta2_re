# Minimap mockup with the same layers as the full map: frame A, E-style map, gang tint, phones and other POIs.
# usage: python3 -I minimap_pois.py <data dir> <captures dir> <work dir> <out dir>
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
DATA, CAPS, WORK, OUT = sys.argv[1:5]
import make_all as M
import fullmap as F
M.setup(DATA, CAPS, WORK)
F.setup(DATA, 'bil', WORK)
import numpy as np
from PIL import Image
import designs
from mm_lib import downsample

BPX = 2.0  # minimap px per block at 1x: zoomed out a little so nearby points of interest show up


def inset_with_pois(d, data, s):
    M.BLOCK_PX = BPX
    m = M.map_inset(d, s)
    H, W, _ = m.shape
    cx, cy = d.center
    px0, py0 = M.PLAYER
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    bx = px0 + (xx / s - cx) / BPX; by = py0 + (yy / s - cy) / BPX
    grid = F.gang_grid(data)
    gi = grid[np.clip(by.astype(int), 0, 255), np.clip(bx.astype(int), 0, 255)]
    for g in data['gangs']:
        if g['arrow'] > 0:
            col = np.array(F.gang_colour(g['arrow'])) / 255
            msk = gi == g['arrow']
            m[msk] = m[msk] * 0.62 + col * 0.38
    img = M.rgba_from(m, M.soft((d.screen_sd < 0).astype(np.float32), s))
    k = 0.42 * s
    I = F.icons(k)
    x0, y0, x1, y1 = d.screen_box

    def put(im, bxx, byy):
        X = (cx + (bxx - px0) * BPX) * s; Y = (cy + (byy - py0) * BPX) * s
        if x0 * s + 2 < X < x1 * s - 2 and y0 * s + 2 < Y < y1 * s - 2:
            img.alpha_composite(im, (int(round(X - im.width / 2)), int(round(Y - im.height / 2))))
    for (x, y, z) in data['frenzies']:
        put(I['frenzy'], x, y)
    for gg in data['garages']:
        put(I['garage'], gg['x'], gg['y'])
    for t, x, y, w, h, n in F.ZONES:
        if t == 16:
            put(I['restart'], x + w / 2, y + h / 2)
    for oid, (x, y, z) in data['phones']:
        put(I['phone_info' if oid == 163 else 'phone_%d' % oid], x, y)
    for (x, y, w, h) in data['save']:
        put(I['save'], x + w / 2, y + h / 2)
    arrow = M.marker_images(s)[0]
    img.alpha_composite(arrow, (int(round(cx * s - arrow.width / 2)), int(round(cy * s - arrow.height / 2))))
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    data = F.parse()
    d = designs.respect_ring(); d.map_style = 'schematic'
    cells = []
    for s, cap in ((1, '43_day'), (1.5, '169_dusk')):
        fr = downsample(d.rgb, d.canvas.alpha, s, 128)
        grgb, ga = d.glass
        gl = M.rgba_from(np.dstack([M.soft(grgb[..., i], s) for i in range(3)]), M.soft(ga, s))
        mm = inset_with_pois(d, data, s); mm.alpha_composite(gl); mm.alpha_composite(fr)
        img, (x, y) = M.place(M.CAPTURES[cap][0], s, mm)
        img.save(os.path.join(OUT, 'minimap_A_pois_%s.png' % cap))
        if s == 1:
            mm.resize((mm.width * 4, mm.height * 4), Image.NEAREST).save(os.path.join(OUT, 'minimap_A_pois_1x_x4.png'))
            c = img.crop((496, 336, 640, 480)); c = c.resize((c.width * 3, c.height * 3), Image.NEAREST)
            cells.append(M.label(c, 'A with the E map and points of interest', 'Industrial, 640x480, corner x3'))
            bg = Image.new('RGBA', mm.size, (40, 44, 46, 255)); bg.alpha_composite(mm)
            cells.append(M.label(bg.resize((mm.width * 4, mm.height * 4), Image.NEAREST), 'same minimap, 1x shown x4', 'church right by the player'))
    # same minimap elsewhere (no matching capture): at the Zaibatsu info phone, inside their territory
    M.PLAYER = (71.0, 181.0)
    fr = downsample(d.rgb, d.canvas.alpha, 1, 128)
    grgb, ga = d.glass
    gl = M.rgba_from(np.dstack([M.soft(grgb[..., i], 1) for i in range(3)]), M.soft(ga, 1))
    mm = inset_with_pois(d, data, 1); mm.alpha_composite(gl); mm.alpha_composite(fr)
    bg = Image.new('RGBA', mm.size, (40, 44, 46, 255)); bg.alpha_composite(mm)
    bg = bg.resize((mm.width * 4, mm.height * 4), Image.NEAREST); bg.save(os.path.join(OUT, 'minimap_A_pois_zaibatsu_1x_x4.png'))
    cells.append(M.label(bg, 'same minimap at the Zaibatsu info phone', 'standalone, territory tint and gang phones'))
    M.hstack(cells).save(os.path.join(OUT, 'minimap_A_pois_sheet.png'))


main()
