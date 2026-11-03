# Whole-level map with gang territories and points of interest, all read from the user's level files:
# zones from the .gmp, phones/save/shops/frenzies/tokens/pickups/gang info from the .scr, icons from the .sty.
# usage: python3 -I fullmap.py <data dir> <level: wil|bil> <work dir> <out dir>   (work dir holds work_map_<level>.png)
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from sty import Sty
from scr import Scr
from gmpzones import zones
import mm_lib
import designs
from mm_lib import font, Canvas, hexrgb, sd_circle, sd_rrect, profile_bevel, profile_round, shade, downsample, glyph_mask

FONT = font(True, 13)
FONT_S = font(False, 12)
LV = STY = SCR = ZONES = BASEMAP = CODE = USER = CODE_REMAP_BASE = None


def setup(data, level, work):
    global LV, STY, SCR, ZONES, BASEMAP, CODE, USER, CODE_REMAP_BASE
    LV = level
    STY = Sty(os.path.join(data, level + '.sty'))
    names = {n.lower(): n for n in os.listdir(data)}
    SCR = Scr(os.path.join(data, names[level + '.scr']))
    ZONES = zones(os.path.join(data, level + '.gmp'))
    BASEMAP = Image.open(os.path.join(work, 'work_map_%s.png' % level)).convert('RGB')  # 8 px per block
    CODE = sum(STY.sprb[:2]); USER = STY.user_base()
    CODE_REMAP_BASE = sum(STY.palb[:4])


# code object model -> first code_obj sprite, from gCodeObjectTemplates (cumulative mNumSprites)
PICKUP_SPRITE = {200: 18, 201: 26, 202: 34, 203: 42, 204: 50, 205: 58, 206: 66, 208: 82, 209: 90, 216: 118, 217: 126,
                 230: 167, 231: 175, 232: 183, 233: 191, 234: 199, 235: 207, 236: 215, 237: 223, 239: 239}
PHONE_SPRITE, TOKEN_SPRITE, FRENZY_SPRITE = 510, 9, 286
PHONE_REMAP = {180: 0, 176: 1, 178: 2}  # green, red, yellow (ObjectDefinitions::CloneRemappedDefinitions)


# ---------------------------------------------------------------- data
def parse():
    d = {'gangs': [], 'phones': [], 'tokens': [], 'frenzies': [], 'pickups': [], 'shops': [], 'save': [], 'start': None}
    hdr = {i: (p, t) for i, p, t in SCR.commands()}
    for i, (p, t) in sorted(hdr.items()):
        if t == 5:
            d['start'] = SCR.fix_vec(p + 12)
        elif t in (223, 322):
            g, rm, w1, w2, w3, ar, kr = struct.unpack_from('<HbBBBBB', SCR.data, p + 8)
            d['gangs'].append({'name': SCR.strings[g][0], 'arrow': ar, 'info': SCR.fix_vec(p + 0x10)})
        elif t in (15, 16, 17, 18, 19, 20):
            oid = struct.unpack_from('<H', SCR.data, p + 0x18)[0]; v = SCR.data[p + 0x1C]; pos = SCR.fix_vec(p + 12)
            if oid in PHONE_REMAP or oid == 163:
                d['phones'].append((oid, pos))
            elif oid == 266:
                d['tokens'].append(pos)
            elif oid == 286:
                d['frenzies'].append(pos)
            elif oid in PICKUP_SPRITE:
                d['pickups'].append((oid, pos))
            elif oid == 130:
                d['shops'].append((v, pos))
        elif t == 214:
            start = struct.unpack_from('<H', SCR.data, p + 0xE)[0]
            x, y, z, w, h = [v / 16384 for v in struct.unpack_from('<5i', SCR.data, p + 0x10)]
            q = start; ops = []
            for _ in range(4):
                if q not in hdr:
                    break
                ops.append(hdr[q][1]); q = struct.unpack_from('<h', SCR.data, hdr[q][0] + 4)[0]
            if 438 in ops:   # the thread starts with DO_SAVE_GAME: the church
                d['save'].append((x, y, w, h))
    # garages: car shop bays within 4 blocks of each other form one garage
    gar = []
    for v, (x, y, z) in d['shops']:
        for g in gar:
            if any(abs(px - x) < 4 and abs(py - y) < 4 for px, py in g['pts']):
                g['bays'].append(v); g['pts'].append((x, y)); break
        else:
            gar.append({'x': x, 'y': y, 'bays': [v], 'pts': [(x, y)]})
    for g in gar:
        g['x'] = np.mean([p[0] for p in g['pts']]); g['y'] = np.mean([p[1] for p in g['pts']])
    d['garages'] = gar
    return d


def gang_colour(arrow):
    im = np.asarray(STY.sprite(USER + 71 + arrow - 1).convert('RGBA')).astype(np.float32)
    px = im[im[..., 3] > 0][:, :3]
    c = px[px.sum(1).argsort()[len(px) // 2:]].mean(0)  # the brighter half of the meter pip
    return tuple(int(v) for v in c)


# ---------------------------------------------------------------- our own HUD-style badges (original art)
G5 = {
    'cross': ["..#..", "#####", "..#..", "..#..", "..#.."],
    'plus': ["..#..", "..#..", "#####", "..#..", "..#.."],
    'bars': ["#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#"],
    'T': ["#####", "..#..", "..#..", "..#..", "..#.."],
    'P': ["####.", "#...#", "####.", "#....", "#...."],
    'G': [".###.", "#....", "#.###", "#...#", ".###."],
}


def badge(kind, scale):
    spec = {'save': ('disc', '#d8c8a0', 'cross', '#5a2010'), 'restart': ('disc', '#f0f0e8', 'plus', '#d02018'),
            'arrest': ('disc', '#2848a8', 'bars', '#e8e8f0'), 'garage': ('square', '#e8b414', 'G', '#141414'),
            'station': ('square', '#40464c', 'T', '#f0f0f0'), 'police': ('shield', '#2848a8', 'P', '#f0f0f0')}[kind]
    shape, col, gl, gcol = spec
    c = Canvas(13, 13)
    if shape == 'disc':
        s = sd_circle(c, 6.5, 6.5, 6.2)
    elif shape == 'square':
        s = sd_rrect(c, 0.4, 0.4, 12.6, 12.6, 2.5)
    else:
        s = np.minimum(sd_rrect(c, 0.6, 0.4, 12.4, 9, 2), mm_lib.sd_poly(c, [(0.6, 7), (12.4, 7), (6.5, 12.8)]))
    c.paint(s < 0, hexrgb('#141008'), height=0.4)
    c.paint(s < -1.0, hexrgb(col), spec=0.6, shin=24, height=0.4 + profile_bevel(-s - 1.0, 1.8, 2.0))
    g = glyph_mask(c, G5[gl], 4.0, 4.0, 1.0)
    c.paint(g * (s < -1.0), hexrgb(gcol), add=-0.5)
    c.alpha[s < 0] = 1
    rgb, _ = shade(c, ambient=0.45, ao_r=1.0, ao_k=0.3)
    return downsample(rgb, c.alpha, scale, 48)


def player_badge(scale):
    c, rgb = designs.player_arrow()
    return downsample(rgb, c.alpha, scale, 32)


# ---------------------------------------------------------------- game sprites, read from the .sty at render time
def spr(idx, vpal=None, scale=1.0):
    im = STY.sprite(idx, vpal).convert('RGBA')
    if scale != 1.0:
        im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))),
                       Image.NEAREST if scale >= 1 else Image.LANCZOS)
    return im


def icons(k):
    """All marker images at icon scale k (1 for the 1024 px map)."""
    I = {}
    I['phone_info'] = spr(CODE + PHONE_SPRITE, scale=0.75 * k)
    for oid, r in PHONE_REMAP.items():
        I['phone_%d' % oid] = spr(CODE + PHONE_SPRITE, CODE_REMAP_BASE + r, 0.75 * k)
    I['token'] = spr(CODE + TOKEN_SPRITE, scale=0.45 * k)
    I['frenzy'] = spr(CODE + FRENZY_SPRITE, scale=0.6 * k)
    for oid, s in PICKUP_SPRITE.items():
        I['pickup_%d' % oid] = spr(CODE + s, scale=0.45 * k)
    for g in range(1, 8):
        I['gang_%d' % g] = spr(USER + 64 + g - 1, scale=1.0 * k)
    for b in ('save', 'restart', 'arrest', 'garage', 'station', 'police'):
        I[b] = badge(b, k)
    I['start'] = player_badge(k)
    return I


# ---------------------------------------------------------------- render
def gang_grid(d):
    names = {g['name']: g for g in d['gangs'] if g['arrow'] > 0}
    grid = np.full((256, 256), -1, np.int32)
    for t, x, y, w, h, n in ZONES:
        if t == 14 and n in names:
            sub = grid[y:y + h, x:x + w]
            sub[sub < 0] = names[n]['arrow']
    return grid


def render(d, ppb, k, tint_a=0.30):
    W = 256 * ppb
    base = np.asarray(BASEMAP.resize((W, W), Image.BOX if ppb < 8 else Image.NEAREST)).astype(np.float32)
    grid = gang_grid(d)
    gbig = np.kron(grid, np.ones((ppb, ppb), np.int32))
    for g in d['gangs']:
        if g['arrow'] <= 0:
            continue
        col = np.array(gang_colour(g['arrow']), np.float32)
        m = gbig == g['arrow']
        base[m] = base[m] * (1 - tint_a) + col * tint_a
        # territory edge: 2 block-px band where the neighbour belongs to another gang
        edge = np.zeros_like(m)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            edge |= m & ~np.roll(np.roll(m, dy * max(1, ppb // 2), 0), dx * max(1, ppb // 2), 1)
        base[edge] = base[edge] * 0.25 + col * 0.75
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).convert('RGBA')
    dr = ImageDraw.Draw(img)
    I = icons(k)

    def put(im, bx, by):
        img.alpha_composite(im, (int(round(bx * ppb - im.width / 2)), int(round(by * ppb - im.height / 2))))
    # zone outlines for restarts
    for t, x, y, w, h, n in ZONES:
        if t in (16, 20):
            col = (240, 240, 232, 200) if t == 16 else (60, 100, 230, 220)
            dr.rectangle((x * ppb, y * ppb, (x + w) * ppb - 1, (y + h) * ppb - 1), outline=col, width=max(1, ppb // 4))
    for (x, y, z) in d['tokens']:
        put(I['token'], x, y)
    for oid, (x, y, z) in d['pickups']:
        put(I['pickup_%d' % oid], x, y)
    for (x, y, z) in d['frenzies']:
        put(I['frenzy'], x, y)
    seen = set()
    for t, x, y, w, h, n in ZONES:
        if t == 6:
            key = n[:6]
            if key not in seen:
                seen.add(key); put(I['station'], x + w / 2, y + h / 2)
        elif t == 10 and (n.startswith('policestation') and n.endswith('1') or n == 'policehq1'):
            put(I['police'], x + w / 2, y + h / 2)
        elif t == 16:
            put(I['restart'], x + w / 2, y + h / 2)
    arrest_done = []
    for t, x, y, w, h, n in ZONES:
        if t == 20:
            cx, cy = x + w / 2, y + h / 2
            if all(abs(cx - a) + abs(cy - b) > 6 for a, b in arrest_done):
                arrest_done.append((cx, cy)); put(I['arrest'], cx, cy)
    for g in d['garages']:
        put(I['garage'], g['x'], g['y'])
    for oid, (x, y, z) in d['phones']:
        if oid != 163:
            put(I['phone_%d' % oid], x, y)
    for oid, (x, y, z) in d['phones']:
        if oid == 163:
            put(I['phone_info'], x, y)
    for g in d['gangs']:
        if g['arrow'] > 0:
            ic = I['gang_%d' % g['arrow']]
            put(ic, g['info'][0] + (ic.width / ppb) * 0.75, g['info'][1] - (ic.height / ppb) * 0.75)
    for (x, y, w, h) in d['save']:
        put(I['save'], x + w / 2, y + h / 2)
    if d['start']:
        put(I['start'], d['start'][0], d['start'][1])
    return img


def density_window(d, size=64):
    pts = [(p[1][0], p[1][1], 3) for p in d['phones']] + [(x, y, 1) for x, y, z in d['tokens'] + d['frenzies']]
    pts += [(g['x'], g['y'], 2) for g in d['garages']] + [(x, y, 4) for x, y, w, h in d['save']]
    pts += [(g['info'][0], g['info'][1], 3) for g in d['gangs'] if g['arrow'] > 0]
    best = (0, 0, 0)
    for x0 in range(0, 256 - size + 1, 8):
        for y0 in range(0, 256 - size + 1, 8):
            s = sum(w for x, y, w in pts if x0 <= x < x0 + size and y0 <= y < y0 + size)
            best = max(best, (s, x0, y0))
    return best[1], best[2]


def legend(d, k=1.0):
    I = icons(2 * k)
    rows = [('gang_%d' % g['arrow'], '%s territory and info phone icon' % g['name'], 'tint: Gang_14 zones (.gmp); colour: respect meter pip, user sprite %d (.sty)' % (70 + g['arrow']))
            for g in d['gangs'] if g['arrow'] > 0]
    rows += [('phone_info', 'gang info phone (blue)', 'SET_GANG_INFO position (.scr); sprite code_obj 510 (.sty)'),
             ('phone_180', 'mission phone, easy (green)', 'OBJ_DATA model 180 (.scr); code_obj 510 + code remap 0'),
             ('phone_178', 'mission phone, medium (yellow)', 'OBJ_DATA model 178; code remap 2'),
             ('phone_176', 'mission phone, hard (red)', 'OBJ_DATA model 176; code remap 1'),
             ('save', 'save point (church)', 'area trigger whose thread starts with DO_SAVE_GAME (.scr); our badge'),
             ('restart', 'restart after death', 'Restart_16 zones (.gmp); our badge, white outline'),
             ('arrest', 'restart after arrest', 'Arrest_Restart_20 zones (.gmp); our badge, blue outline'),
             ('garage', 'garage (spray / bombs / guns...)', 'car_shop_130 objects grouped (.scr); our badge'),
             ('station', 'train station', 'Railway platform zones (.gmp); our badge'),
             ('frenzy', 'kill frenzy', 'OBJ_DATA model 286 (.scr); code_obj 286 (.sty)'),
             ('token', 'secret token', 'OBJ_DATA model 266 (.scr); code_obj 9 (.sty)'),
             ('pickup_230', 'weapon and power-up pickups', 'OBJ_DATA models 200-239 (.scr); first frame of their code_obj'),
             ('start', 'player start', 'PLAYER_PED (.scr); our arrow')]
    if any(t == 10 and n.startswith('police') for t, x, y, w, h, n in ZONES):
        rows.insert(-3, ('police', 'police station', 'Information_10 zones named police* (.gmp); our badge'))
    H = 46 * len(rows) + 40
    img = Image.new('RGBA', (760, H), (24, 24, 26, 255)); dr = ImageDraw.Draw(img)
    dr.text((10, 8), 'Legend: %s (%s)' % ({'wil': 'Downtown', 'bil': 'Industrial', 'ste': 'Residential'}[LV], LV), font=FONT, fill=(235, 220, 160))
    for i, (key, text, src) in enumerate(rows):
        y = 36 + i * 46; im = I[key]
        if im.height > 40:
            im = im.resize((int(im.width * 40 / im.height), 40), Image.LANCZOS)
        img.alpha_composite(im, (30 - im.width // 2, y + 20 - im.height // 2))
        dr.text((64, y + 4), text, font=FONT, fill=(235, 235, 235))
        dr.text((64, y + 22), src, font=FONT_S, fill=(170, 170, 170))
    return img


def main(out):
    os.makedirs(out, exist_ok=True)
    pins = os.path.join(out, 'pins'); os.makedirs(pins, exist_ok=True)
    for k in (1, 2):
        for b in ('save', 'restart', 'arrest', 'garage', 'station', 'police'):
            badge(b, k).save(os.path.join(pins, '%s_%dx.png' % (b, k)))
    d = parse()
    full = render(d, 4, 1.0)
    full.convert('RGB').save(os.path.join(out, 'map_%s_1024.png' % LV))
    x0, y0 = density_window(d)
    big = render(d, 8, 2.0)
    big.crop((x0 * 8, y0 * 8, (x0 + 64) * 8, (y0 + 64) * 8)).convert('RGB').save(os.path.join(out, 'map_%s_crop2x_%d_%d.png' % (LV, x0, y0)))
    legend(d).convert('RGB').save(os.path.join(out, 'legend_%s.png' % LV))
    with open(os.path.join(out, 'pois_%s.txt' % LV), 'w') as f:
        f.write('start %s\n' % (d['start'],))
        for g in d['gangs']:
            f.write('gang %s arrow %d info %s colour %s\n' % (g['name'], g['arrow'], g['info'], gang_colour(g['arrow']) if g['arrow'] > 0 else None))
        for oid, p in d['phones']:
            f.write('phone %d %s\n' % (oid, p))
        for s in d['save']:
            f.write('save %s\n' % (s,))
        for g in d['garages']:
            f.write('garage %.1f %.1f bays %s\n' % (g['x'], g['y'], g['bays']))
        f.write('frenzies %d tokens %d pickups %d\n' % (len(d['frenzies']), len(d['tokens']), len(d['pickups'])))
        for t, x, y, w, h, n in ZONES:
            if t in (6, 10, 14, 16, 20):
                f.write('zone %d %d %d %d %d %s\n' % (t, x, y, w, h, n))
    print(LV, 'crop at', x0, y0, 'gangs', [(g['name'], g['arrow']) for g in d['gangs']])
    return d


if __name__ == '__main__':
    setup(*sys.argv[1:4])
    main(sys.argv[4])
