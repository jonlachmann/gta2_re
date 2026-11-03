# Builds the frames at 1x/2x, glass overlays, markers, close-ups, in-game composites, per-design sheets and
# the overviews. The map and gang icons come from the user's data at this step only.
# usage: python3 -I make_all.py <data dir> <captures dir> <out dir>   (out dir holds work_map_bil.png first)
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import designs, mm_lib
from mm_lib import SS, downsample, font
from sty import Sty

PLAYER = (44.0, 102.8)       # block coords in bil.gmp of the captures, from the in-game coordinate display
MAP_PX = 8                   # px per block in work_map_bil.png
BLOCK_PX = 3.0               # minimap px per block at 1x
MARGIN = 6                   # HUD px from the screen edges
DATA = CAPS = OUT = MAP = STY = HGT = GND = SCHEM = NIGHT = GPSCAT = None


FONT = font(True, 15)
FONT_S = font(False, 13)

NOTES = {'colour': 'map: lid colours', 'lcd': 'map: 4-tone green LCD', 'schematic': 'map: high contrast by block kind',
         'phosphor': 'map: phosphor green with sweep', 'night': 'map: night dash, amber roads', 'gps': 'map: grey LCD by block kind'}

CAPTURES = {
    '43_day': ('cap_day43/gl_01200.png', 1.0),
    '43_dusk': ('cap_dusk43/gl_01200.png', 1.0),
    '169_day': ('cap_day169/gl_01200.png', 1.5),
    '169_dusk': ('cap_dusk169/gl_01200.png', 1.5),
    '43_brief': ('cap_day43/gl_00600.png', 1.0),
}

LCD = np.array([[0x24, 0x38, 0x14], [0x3a, 0x60, 0x1c], [0x58, 0x88, 0x26], [0x7e, 0xa8, 0x3c]], np.float32)
GPS = np.array([[0x2a, 0x31, 0x24], [0x56, 0x62, 0x49], [0x8e, 0x9a, 0x7c], [0xb9, 0xc3, 0xa3]], np.float32)


def category_map(pal, tex_k, edge_k=0.55, shadow_k=0.7, tall_k=0.06):
    """Map at MAP_PX per block coloured by block kind (water, road, pavement, field, building by height),
    with dark building rims and a cast shadow toward the bottom right; tex_k blends the real lid colours."""
    h, g = HGT, GND
    col = np.zeros((256, 256, 3), np.float32)
    ground = (h >= 1) & (h <= 2)
    col[:] = pal['pavement']
    col[ground & (g == 1)] = pal['road']
    col[ground & (g == 3)] = pal['field']
    col[h == 0] = pal['water']
    b = h >= 3
    col[b] = np.array(pal['building']) + tall_k * (h[b] - 3)[:, None]
    big = np.kron(col, np.ones((MAP_PX, MAP_PX, 1), np.float32))
    H8 = np.kron(h, np.ones((MAP_PX, MAP_PX), np.float32))
    rim = np.zeros_like(H8, bool)
    for dy, dx in ((2, 0), (-2, 0), (0, 2), (0, -2)):
        rim |= H8 > np.roll(np.roll(H8, dy, 0), dx, 1) + 0.5
    big[rim] *= edge_k
    sh = np.roll(np.roll(H8, 4, 0), 4, 1) > H8 + 0.5
    big[sh & ~rim] *= shadow_k
    tex = np.asarray(MAP).astype(np.float32) / 255
    return np.clip(big * (1 - tex_k) + tex * tex_k * big.mean(-1, keepdims=True) / np.maximum(tex.mean(-1, keepdims=True), 0.05), 0, 1)


def _img(a):
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))


def setup(data, caps, out):
    global DATA, CAPS, OUT, MAP, STY, HGT, GND, SCHEM, NIGHT, GPSCAT
    DATA, CAPS, OUT = data, caps, out
    MAP = Image.open(os.path.join(out, 'work_map_bil.png')).convert('RGB')
    STY = Sty(os.path.join(data, 'bil.sty'))
    HGT = np.load(os.path.join(out, 'work_map_bil_h.npy')); GND = np.load(os.path.join(out, 'work_map_bil_g.npy'))
    SCHEM = _img(category_map({'water': (0.10, 0.22, 0.38), 'road': (0.78, 0.76, 0.70), 'pavement': (0.55, 0.53, 0.49),
                               'field': (0.34, 0.50, 0.22), 'building': (0.30, 0.33, 0.39)}, 0.18))
    NIGHT = _img(category_map({'water': (0.03, 0.06, 0.13), 'road': (0.92, 0.56, 0.12), 'pavement': (0.32, 0.21, 0.08),
                               'field': (0.10, 0.15, 0.07), 'building': (0.10, 0.11, 0.14)}, 0.0, edge_k=0.5, shadow_k=0.6, tall_k=0.025))
    GPSCAT = _img(category_map({'water': (0.0, 0.0, 0.0), 'road': (1.0, 1.0, 1.0), 'pavement': (0.67, 0.67, 0.67),
                                'field': (0.67, 0.67, 0.67), 'building': (0.33, 0.33, 0.33)}, 0.0, edge_k=0.0, shadow_k=1.0, tall_k=0.0))


def soft(arr, scale):
    """Downsample an SS-resolution float array to `scale` x native with box filtering."""
    H, W = arr.shape
    return np.asarray(Image.fromarray(arr.astype(np.float32), 'F').resize(
        (int(round(W / SS * scale)), int(round(H / SS * scale))), Image.BOX))


def rgba_from(rgb, a):
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 1) * 255, np.clip(a, 0, 1) * 255]).astype(np.uint8), 'RGBA')


def map_inset(d, scale):
    """Map crop for the screen box, styled; RGBA sized like the design at `scale`."""
    x0, y0, x1, y1 = d.screen_box
    w = int(round((x1 - x0) * scale)); h = int(round((y1 - y0) * scale))
    bw = (x1 - x0) / BLOCK_PX; bh = (y1 - y0) / BLOCK_PX
    cx, cy = PLAYER[0] * MAP_PX, PLAYER[1] * MAP_PX
    box = (cx - bw / 2 * MAP_PX, cy - bh / 2 * MAP_PX, cx + bw / 2 * MAP_PX, cy + bh / 2 * MAP_PX)
    src = {'schematic': SCHEM, 'phosphor': SCHEM, 'night': NIGHT, 'gps': GPSCAT}.get(d.map_style, MAP)
    m = np.asarray(src.resize((w, h), Image.NEAREST if d.map_style == 'gps' else Image.BOX, box=box)).astype(np.float32) / 255
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    if d.map_style == 'gps':
        q = np.clip(np.round(m.mean(-1) * 3).astype(int), 0, 3)
        m = GPS[q] / 255
    elif d.map_style == 'phosphor':
        lum = m @ np.array([0.30, 0.59, 0.11])
        m = np.array([0.02, 0.07, 0.03]) + (lum ** 1.3)[..., None] * np.array([0.30, 0.85, 0.40])
        glow = np.dstack([np.asarray(Image.fromarray((m[..., i] * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.6 * scale))) for i in range(3)]) / 255
        m = m + 0.45 * glow
        # range rings and the sweep, around the player
        px, py = (d.center[0] - x0) * scale, (d.center[1] - y0) * scale
        rr = np.hypot(xx - px, yy - py) / scale
        rings = (np.abs(rr % 15 - 7.5) > 7.0) & (rr > 3)
        m = m + rings[..., None] * np.array([0.0, 0.10, 0.04])
        an = (np.arctan2(yy - py, xx - px) - (-0.75)) % (2 * np.pi)
        trail = np.exp(-(2 * np.pi - an) / 0.55) * (an > np.pi)
        m = m * (0.75 + 0.5 * trail[..., None]) + trail[..., None] * np.array([0.05, 0.22, 0.08])
        head = ((2 * np.pi - an) * rr * scale < 1.0 * scale) & (an > np.pi)
        m[head] = np.array([0.55, 1.0, 0.65])
        sl = (yy.astype(int) % max(2, int(round(scale * 2)))) == 0
        m = m * np.where(sl, 0.78 if scale >= 1.5 else 0.88, 1.0)[..., None]
    elif d.map_style == 'lcd':
        lum = m @ np.array([0.30, 0.59, 0.11])
        lum = (lum - lum.min()) / max(1e-4, lum.max() - lum.min())
        q = np.clip((lum * 4).astype(int), 0, 3)
        m = LCD[q] / 255
        # faint LCD cell grid at native pixel pitch
        if scale >= 1.5:
            yy, xx = np.mgrid[0:h, 0:w]
            grid = ((xx % int(round(scale)) == 0) | (yy % int(round(scale)) == 0))
            m = m * np.where(grid, 0.9, 1.0)[..., None]
    else:
        m = np.clip((m - 0.5) * 1.12 + 0.5, 0, 1) * 0.95
    W = int(round(d.W * scale)); H = int(round(d.H * scale))
    out = np.zeros((H, W, 3), np.float32)
    ox = int(round(x0 * scale)); oy = int(round(y0 * scale))
    out[oy:oy + h, ox:ox + w] = m[:H - oy, :W - ox]
    return out


def marker_images(scale):
    c, rgb = designs.player_arrow()
    arrow = downsample(rgb, c.alpha, scale, 32)
    # mission blip: a small bevelled red dome
    b = mm_lib.Canvas(7, 7)
    s = mm_lib.sd_circle(b, 3.5, 3.5, 3.3)
    b.paint(s < 0, mm_lib.hexrgb('#1a0a06'), height=0.3)
    b.paint(s < -0.9, mm_lib.hexrgb('#e02818'), spec=0.9, shin=30, height=0.3 + mm_lib.profile_bevel(-s - 0.9, 2.4, 1.6))
    b.alpha[s < 0] = 1
    brgb, _ = mm_lib.shade(b, ambient=0.4, ao_r=1.0)
    blip = downsample(brgb, b.alpha, scale, 24)
    return arrow, blip


def gang_icon(scale):
    """Zone gang icon for design D, read from the user's style at composite time (user sprite 66)."""
    im = STY.sprite(STY.user_base() + 66)
    return im.resize((int(round(im.width * scale)), int(round(im.height * scale))), Image.NEAREST)


def compose(d, scale, frame, glass, with_map=True):
    """Full minimap at `scale`: map, markers, glass, frame (and the gang icon for D)."""
    W, H = frame.size
    base = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    if with_map:
        scr = soft((d.screen_sd < 0).astype(np.float32), scale)
        base = rgba_from(map_inset(d, scale), scr)
        arrow, blip = marker_images(scale)
        if d.map_style == 'lcd':
            def lcdify(im, inner):
                ar = np.asarray(im).astype(np.float32)
                lum = ar[..., :3].mean(-1) / 255
                ar[..., :3] = np.where((lum > 0.35)[..., None], LCD[inner] * 0.85, LCD[0] * 0.55)
                return Image.fromarray(ar.astype(np.uint8), 'RGBA')
            arrow = lcdify(arrow, 0); blip = lcdify(blip, 3)
        elif d.map_style in ('gps', 'phosphor'):
            if d.map_style == 'gps':
                fa, ra, fb = GPS[0] * 0.9, GPS[0] * 0.6, GPS[3]
            else:
                fa, ra, fb = np.array([190, 255, 200]), np.array([0, 60, 20]), np.array([220, 255, 225])
            def recol(im, fill, rim):
                ar = np.asarray(im).astype(np.float32)
                lum = ar[..., :3].mean(-1) / 255
                ar[..., :3] = np.where((lum > 0.35)[..., None], fill, rim)
                return Image.fromarray(ar.astype(np.uint8), 'RGBA')
            arrow = recol(arrow, fa, ra); blip = recol(blip, fb, ra)
        cx, cy = d.center
        bx, by = cx + 7 * BLOCK_PX, cy - 5 * BLOCK_PX
        base.alpha_composite(blip, (int(round(bx * scale - blip.width / 2)), int(round(by * scale - blip.height / 2))))
        base.alpha_composite(arrow, (int(round(cx * scale - arrow.width / 2)), int(round(cy * scale - arrow.height / 2))))
        if d.map_style == 'phosphor':
            gl = designs.GLYPHS['N']; z = max(1, int(round(scale)))
            nx = int(round(d.center[0] * scale)) - 2 * z - z // 2; ny = int(round((d.screen_box[1] + 2) * scale))
            dr = ImageDraw.Draw(base)
            for j, row in enumerate(gl):
                for i, ch in enumerate(row):
                    if ch == '#':
                        dr.rectangle((nx + i * z, ny + j * z, nx + (i + 1) * z - 1, ny + (j + 1) * z - 1), fill=(200, 255, 210, 255))
        base.alpha_composite(glass)
    base.alpha_composite(frame)
    if hasattr(d, 'socket'):
        ic = gang_icon(scale)
        sx, sy, _ = d.socket
        base.alpha_composite(ic, (int(round(sx * scale - ic.width / 2)), int(round(sy * scale - ic.height / 2))))
    return base


def place(capture, scale, mm):
    cap = Image.open(os.path.join(CAPS, capture)).convert('RGBA')
    x = int(cap.width - mm.width - MARGIN * scale); y = int(cap.height - mm.height - MARGIN * scale)
    cap.alpha_composite(mm, (x, y))
    return cap.convert('RGB'), (x, y)


def checker(w, h, a=(58, 62, 66), b=(70, 74, 78), cell=8):
    yy, xx = np.mgrid[0:h, 0:w]
    m = ((xx // cell + yy // cell) % 2)[..., None]
    return Image.fromarray(np.where(m, np.array(b), np.array(a)).astype(np.uint8), 'RGB').convert('RGBA')


def closeup(img, z, bg=None):
    big = img.resize((img.width * z, img.height * z), Image.NEAREST)
    out = bg if bg is not None else checker(big.width, big.height)
    out.alpha_composite(big)
    return out


def label(im, text, sub=None):
    pad = 26 if sub is None else 44
    out = Image.new('RGB', (im.width, im.height + pad), (24, 24, 26))
    out.paste(im.convert('RGB'), (0, pad))
    dr = ImageDraw.Draw(out)
    dr.text((6, 4), text, font=FONT, fill=(235, 220, 160))
    if sub:
        dr.text((6, 24), sub, font=FONT_S, fill=(190, 190, 190))
    return out


def hstack(ims, gap=12, bg=(24, 24, 26)):
    W = sum(i.width for i in ims) + gap * (len(ims) - 1); H = max(i.height for i in ims)
    out = Image.new('RGB', (W, H), bg); x = 0
    for i in ims:
        out.paste(i.convert('RGB'), (x, 0)); x += i.width + gap
    return out


def vstack(ims, gap=12, bg=(24, 24, 26)):
    W = max(i.width for i in ims); H = sum(i.height for i in ims) + gap * (len(ims) - 1)
    out = Image.new('RGB', (W, H), bg); y = 0
    for i in ims:
        out.paste(i.convert('RGB'), (0, y)); y += i.height + gap
    return out


def main():
    rows = []
    marker_dir = os.path.join(OUT, 'markers'); os.makedirs(marker_dir, exist_ok=True)
    for s in (1, 2):
        a, b = marker_images(s)
        a.save(os.path.join(marker_dir, 'player_arrow_%dx.png' % s)); b.save(os.path.join(marker_dir, 'blip_%dx.png' % s))
    rows2 = []
    for build in designs.BUILDERS + designs.BUILDERS2:
        d = build()
        out = os.path.join(OUT, d.name); os.makedirs(out, exist_ok=True)
        frames, glasses, minis = {}, {}, {}
        for s in (1, 1.5, 2):
            frames[s] = downsample(d.rgb, d.canvas.alpha, s, 128)
            grgb, ga = d.glass
            glasses[s] = rgba_from(np.dstack([soft(grgb[..., i], s) for i in range(3)]), soft(ga, s))
            minis[s] = compose(d, s, frames[s], glasses[s])
        for s in (1, 2):
            frames[s].save(os.path.join(out, 'frame_%dx.png' % s))
            glasses[s].save(os.path.join(out, 'glass_%dx.png' % s))
            minis[s].save(os.path.join(out, 'minimap_%dx.png' % s))
            m = soft((d.screen_sd < 0).astype(np.float32), s)
            Image.fromarray((m * 255).astype(np.uint8), 'L').save(os.path.join(out, 'screen_mask_%dx.png' % s))
        comps = {}
        for k, (capf, s) in CAPTURES.items():
            img, pos = place(capf, s, minis[s])
            img.save(os.path.join(out, 'composite_%s.png' % k)); comps[k] = (img, pos)
        # close-ups: the bare frame at 1x enlarged 6x, and the assembled minimap at 1x enlarged 4x
        z_frame = closeup(frames[1], 6)
        z_mini = closeup(minis[1], 4, bg=Image.new('RGBA', (minis[1].width * 4, minis[1].height * 4), (40, 44, 46, 255)))
        z_frame.save(os.path.join(out, 'closeup_frame_1x_x6.png'))
        z_mini.save(os.path.join(out, 'closeup_minimap_1x_x4.png'))
        closeup(frames[2], 3).save(os.path.join(out, 'closeup_frame_2x_x3.png'))
        # corner crop of the 16:9 dusk composite at 2x nearest, to judge it against the real HUD scale
        img, (x, y) = comps['169_dusk']
        crop = img.crop((x - 60, y - 40, img.width, img.height))
        crop2 = crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST)
        sheet = vstack([
            hstack([label(z_frame, d.title + ': frame alone, 1x HUD pixels shown x6'),
                    label(z_mini, 'with map, markers and glass, 1x shown x4')]),
            hstack([label(comps['43_day'][0], '640x480 noon, HUD 1x'),
                    label(comps['43_dusk'][0], '640x480 dusk')]),
            hstack([label(comps['169_day'][0].resize((640, 360), Image.BOX), '1280x720 noon (HUD 1.5x), shown at half size'),
                    label(crop2.resize((min(640, crop2.width), int(crop2.height * min(640, crop2.width) / crop2.width)), Image.NEAREST),
                          '1280x720 dusk, corner x2')]),
        ])
        sheet.save(os.path.join(out, 'sheet.png'))
        img43, (x43, y43) = comps['43_day']
        corner = img43.crop((x43 - 24, y43 - 24, 640, 480))
        fixed = img43.crop((496, 336, 640, 480))
        (rows if build in designs.BUILDERS else rows2).append((d, closeup(frames[1], 4), img43, corner.resize((corner.width * 3, corner.height * 3), Image.NEAREST),
                                                              fixed.resize((fixed.width * 3, fixed.height * 3), Image.NEAREST)))
        print(d.name, frames[1].size)
    # overviews: same capture for every design, close-up of the frame next to it
    for name, rs in (('overview.png', rows), ('overview2.png', rows2)):
        blocks = []
        for d, zf, comp, corner, _ in rs:
            blocks.append(hstack([label(zf, d.title, 'frame alone: 1x HUD pixels shown x4, nearest'),
                                  label(comp, 'in game: 640x480 noon, HUD 1x', 'map: bil.gmp around the player, ' + NOTES[d.map_style].replace('map: ', '')),
                                  label(corner, 'in game corner, x3 nearest', 'what the player actually sees, pixel for pixel')]))
        vstack(blocks, gap=18).save(os.path.join(OUT, name))
    # all eight, same 144x144 corner of the same capture, x3
    cells = [label(f, d.title, NOTES[d.map_style]) for d, _, _, _, f in rows + rows2]
    vstack([hstack(cells[:4]), hstack(cells[4:])], gap=12).save(os.path.join(OUT, 'overview_all.png'))

if __name__ == "__main__":
    setup(*sys.argv[1:4])
    main()
