# The eight minimap frame designs. Each build returns a Design: shaded supersampled frame, screen mask,
# glass overlay, and where the map and markers go, all in native HUD pixels (640x480 HUD space).
import numpy as np
from mm_lib import (SS, Canvas, hexrgb, sd_circle, sd_rrect, sd_chamfer, blur, profile_round,
                    profile_bevel, shade, glyph_mask, GLYPHS, sd_poly)


class Design:
    pass


def _glass(c, screen_sd, cx, cy, kind='round', sheen_k=0.20, shadow_k=0.55, edge_k=0.25):
    """Glass overlay over the screen: inner shadow cast by the bezel (light from the top left) and a soft sheen."""
    inside = np.clip(-screen_sd * SS, 0, 1)
    # shadow of the bezel lip: the screen edge offset toward the bottom right
    off = blur((screen_sd > -0.0).astype(np.float32), SS)  # 1 near/outside the edge
    sh = np.roll(np.roll(off, int(1.6*SS), 0), int(1.4*SS), 1)
    shadow = np.clip(sh, 0, 1) * shadow_k
    rim = np.clip(1 - (-screen_sd) / 3.0, 0, 1) ** 2 * edge_k   # general vignette at the edge
    a_dark = np.clip(shadow + rim, 0, 0.75) * inside
    # sheen: a soft elliptical highlight in the upper left of the glass
    if kind == 'round':
        r = np.hypot((c.x - cx) / 1.0, (c.y - cy) / 1.0)
        R = -np.min(screen_sd)
    else:
        R = 1
    sx = (c.x - (cx - 0.35 * c.sw)) / (0.55 * c.sw)
    sy = (c.y - (cy - 0.45 * c.sh)) / (0.30 * c.sh)
    sheen = np.clip(1 - np.hypot(sx, sy), 0, 1) ** 1.5 * sheen_k * inside
    rgb = np.zeros(c.x.shape + (3,), np.float32)
    a = a_dark + sheen
    rgb[:] = 0.0
    w = sheen / np.maximum(a, 1e-4)
    rgb += w[..., None] * np.array([1.0, 1.0, 0.95])
    return rgb, a


def respect_ring():
    """A: the respect meter bent into a ring. Bronze lips, ribbed glass tube, a bright end cap with N."""
    W, H = 96, 101
    c = Canvas(W, H)
    cx, cy = 48.0, 47.0
    r = np.hypot(c.x - cx, c.y - cy)
    ang = np.arctan2(c.y - cy, c.x - cx)
    bronze = hexrgb('#86704c'); bronze_dk = hexrgb('#5a4830')
    tube = hexrgb('#2a6168')
    # lamp bracket under the ring
    br = sd_rrect(c, 28, 86, 68, 99, 4)
    c.paint(br < 0, bronze_dk, spec=0.25, shin=14, height=profile_bevel(-br, 2.0, 2.0))
    for i, col in enumerate(['#38c018', '#c07810', '#b81808']):
        x0 = 32 + i * 11.5
        lm = sd_rrect(c, x0, 91, x0 + 8.5, 96.5, 1.5)
        lit = i == 0
        base = hexrgb(col)
        c.paint(lm < 0, tuple(v * (1.0 if lit else 0.45) for v in base), spec=0.35, shin=40,
                height=2.0 + profile_round(-lm, 2.0, 0.8))
        if lit:
            c.emit[lm < 0] += np.array(base) / 255.0 * 0.12
    # outer bronze lip
    m = (r < 46) & (r >= 43)
    c.paint(m, bronze, spec=0.35, shin=16, height=profile_round(46 - r, 3.0, 3.2))
    # ribbed glass tube: colour bands like the meter's segments, a lit core, a hard specular
    m = (r < 43) & (r >= 35.5)
    nrib = 70
    rib = 0.62 + 0.38 * (0.5 + 0.5 * np.cos(ang * nrib)) ** 0.6
    c.paint(m, tube, spec=1.1, shin=40, height=1.0 + profile_round(43 - r, 7.5, 3.6) + 0.12 * np.cos(ang * nrib))
    c.alb[m] *= rib[m][:, None]
    core = np.clip(1 - np.abs(r - 39.6) / 1.6, 0, 1) * m
    c.emit += (core * rib)[..., None] * np.array(tube) / 255.0 * 0.45
    # end cap at the top: brighter tube colour with a dark engraved N
    cap = m & (np.abs(ang + np.pi / 2) < 0.19)
    c.paint(cap, tuple(min(255, v * 2.3) for v in tube), spec=0.9, shin=30,
            height=1.4 + profile_round(43 - r, 7.5, 3.6))
    c.emit[cap] *= 0
    seams = m & (np.abs(np.abs(ang + np.pi / 2) - 0.19) < 0.026)
    c.paint(seams, bronze_dk, height=1.0)
    g = glyph_mask(c, GLYPHS['N'], cx - 2.5, 5.0, 1.0) * cap
    c.paint(g, hexrgb('#0c1416'), spec=0.0, add=-1.4)
    # inner lip
    m = (r < 35.5) & (r >= 33)
    c.paint(m, bronze, spec=0.35, shin=16, height=profile_round(35.5 - r, 2.5, 2.2))
    c.alpha[(r < 46)] = 1
    hole = r < 33
    c.alpha[hole] = 0
    d = Design()
    d.name = 'A_respect_ring'; d.title = 'A  Respect ring'
    d.canvas = c; d.W, d.H = W, H
    d.rgb, _ = shade(c)
    d.screen_sd = r - 33.0
    d.screen_box = (cx - 33, cy - 33, cx + 33, cy + 33)
    d.center = (cx, cy)
    c.sw, c.sh = 66, 66
    d.glass = _glass(c, d.screen_sd, cx, cy)
    d.map_style = 'colour'
    d.rotates = True
    return d


def pager_handset():
    """B: a handheld pager-style unit in the same dark plastic, with a khaki metal screen bezel and an LCD."""
    W, H = 104, 95
    c = Canvas(W, H)
    body = sd_rrect(c, 1, 4, 103, 94, 10)
    plastic = hexrgb('#1c2620')
    c.paint(body < 0, plastic, spec=0.35, shin=12, height=profile_bevel(-body, 5.5, 5.5))
    # moulded seam a little inside the shell edge
    seam = np.abs(body + 3.0) < 0.35
    c.h_[seam] -= 0.6
    # top grip ridge with an orange rubber button at the right
    btn = sd_rrect(c, 78, 0.5, 96, 9, 3)
    bm = (btn < 0) & (body > -3.5)
    c.paint(bm, hexrgb('#d0601c'), spec=0.6, shin=22, height=profile_bevel(-btn, 2.0, 4.0))
    # speaker slots bottom left
    for i in range(4):
        sl = sd_rrect(c, 9 + i * 5, 86.5, 11.5 + i * 5, 90.5, 1.1)
        c.paint(sl < 0, hexrgb('#0b100d'), add=-2.0)
    # red LED bottom right
    led = sd_circle(c, 95, 88.5, 1.9)
    c.paint(led < 0, hexrgb('#ff3818'), spec=0.9, shin=50, height=5.5 + profile_round(-led, 1.9, 0.8))
    c.emit[led < 0] += np.array([0.5, 0.12, 0.05])
    # metal bezel around the screen
    bez = sd_rrect(c, 8, 11, 96, 85, 6)
    c.paint(bez < 0, hexrgb('#8e876b'), spec=0.6, shin=26, height=5.0 - 1.8 + profile_bevel(-bez, 1.6, 1.3))
    ledge = sd_rrect(c, 11, 14, 93, 82, 4)
    c.paint(ledge < 0, hexrgb('#3a3f30'), spec=0.2, shin=10, height=2.0)
    # N printed on the bezel top edge, centred
    g = glyph_mask(c, GLYPHS['N'], 52 - 2.5, 5.0, 1.0)
    c.paint(g, hexrgb('#c8c39a'), spec=0.2, add=0.0)
    c.alpha[body < 0] = 1
    c.alpha[bm] = 1
    screen = sd_rrect(c, 13, 16, 91, 80, 3)
    c.alpha[screen < 0] = 0
    d = Design()
    d.name = 'B_pager_handset'; d.title = 'B  Pager handset'
    d.canvas = c; d.W, d.H = W, H
    d.rgb, _ = shade(c)
    d.screen_sd = screen
    d.screen_box = (13, 16, 91, 80)
    d.center = (52, 48)
    c.sw, c.sh = 78, 64
    d.glass = _glass(c, screen, 52, 48, 'rect')
    d.map_style = 'lcd'
    d.rotates = False
    return d


def steel_plate():
    """C: an industrial riveted gunmetal plate with cut corners, screws and a N tab."""
    W, H = 98, 102
    c = Canvas(W, H)
    plate = sd_chamfer(c, 1, 5, 97, 101, 13)
    steel = hexrgb('#56636a')
    # brushed grain
    grain = 0.86 + 0.14 * np.sin(c.y * 9.1 + np.sin(c.x * 0.7) * 2.0) * np.sin(c.y * 2.3 + 1.7)
    c.paint(plate < 0, steel, spec=0.55, shin=22, height=profile_bevel(-plate, 3.5, 4.0))
    c.alb *= grain[..., None]
    mottle = blur(np.random.default_rng(7).random(c.x.shape).astype(np.float32), 3 * SS)
    mottle = (mottle - mottle.mean()) / (mottle.std() + 1e-6)
    c.alb *= np.clip(1 + 0.10 * mottle, 0.7, 1.3)[..., None]
    # N tab on top
    tab = sd_rrect(c, 39, 0, 59, 12, 3)
    c.paint(tab < 0, hexrgb('#b8a032'), spec=0.6, shin=24, height=np.maximum(profile_bevel(-tab, 1.8, 5.0), c.h_))
    g = glyph_mask(c, GLYPHS['N'], 49 - 2.5, 2.5, 1.0)
    c.paint(g * (tab < 0), hexrgb('#1a1608'), add=-1.0)
    # screws in the four corners: domed with a cross slot
    for sx, sy in ((10, 14), (88, 14), (10, 92), (88, 92)):
        s = sd_circle(c, sx, sy, 3.2)
        c.paint(s < 0, hexrgb('#9aa3a6'), spec=0.9, shin=36, height=4.0 + profile_round(-s, 3.2, 1.6))
        cross = ((np.abs(c.x - sx + (c.y - sy)) < 0.55) | (np.abs(c.x - sx - (c.y - sy)) < 0.55)) & (s < -0.6)
        c.paint(cross, hexrgb('#2a3034'), add=-1.2)
    # recessed screen well with a plain inner lip
    well = sd_rrect(c, 12, 16, 86, 90, 5)
    c.paint(well < 0, hexrgb('#2c3438'), spec=0.3, shin=14, height=4.0 - 2.5 * np.clip(-well / 2.0, 0, 1))
    lip = sd_rrect(c, 15, 19, 83, 87, 3)
    c.paint(lip < 0, hexrgb('#1a1f22'), height=1.5)
    # recessed hazard stripe panel along the bottom edge
    hp = sd_rrect(c, 20, 92, 78, 98.5, 1.2)
    stripe = ((c.x + c.y) / 3.2) % 2 < 1
    c.paint((hp < 0) & stripe, hexrgb('#c8a018'), spec=0.4, shin=16, height=2.6)
    c.paint((hp < 0) & ~stripe, hexrgb('#1c1a14'), spec=0.4, shin=16, height=2.6)
    c.alpha[plate < 0] = 1; c.alpha[tab < 0] = 1
    screen = sd_rrect(c, 16, 20, 82, 86, 2)
    c.alpha[screen < 0] = 0
    d = Design()
    d.name = 'C_steel_plate'; d.title = 'C  Steel plate'
    d.canvas = c; d.W, d.H = W, H
    d.rgb, _ = shade(c)
    d.screen_sd = screen
    d.screen_box = (16, 20, 82, 86)
    d.center = (49, 53)
    c.sw, c.sh = 66, 66
    d.glass = _glass(c, screen, 49, 53, 'rect')
    d.map_style = 'colour'
    d.rotates = False
    return d


def compass_dial(gang_rgb=(214, 196, 40)):
    """D: a compass dial: polished ring with ticks and NESW, plus a socket for the zone's gang icon."""
    W, H = 104, 100
    c = Canvas(W, H)
    cx, cy = 48.0, 48.0
    r = np.hypot(c.x - cx, c.y - cy); ang = np.arctan2(c.y - cy, c.x - cx)
    # dark outer housing ring
    m = r < 47
    c.paint(m, hexrgb('#1d2224'), spec=0.4, shin=20, height=profile_bevel(47 - r, 2.5, 3.0))
    # polished ring
    m = (r < 44.5) & (r >= 34)
    c.paint(m, hexrgb('#7c868a'), spec=0.75, shin=26, height=3.0 + profile_round(44.5 - r, 10.5, 2.4))
    # engraved ticks every 15 degrees, long every 90
    t = np.abs(((ang + np.pi / 2) / (np.pi / 12) + 0.5) % 1 - 0.5) * (np.pi / 12) * r
    tick = m & (t < 0.45) & (r > 41.0)
    c.paint(tick, hexrgb('#262c2e'), add=-0.8)
    for letter, a in (('N', -np.pi / 2), ('E', 0.0), ('S', np.pi / 2), ('W', np.pi)):
        gx = cx + 38.6 * np.cos(a); gy = cy + 38.6 * np.sin(a)
        gw = len(GLYPHS[letter][0])
        g = glyph_mask(c, GLYPHS[letter], gx - gw / 2, gy - 2.5, 1.0)
        col = hexrgb('#e8301c') if letter == 'N' else hexrgb('#20262a')
        c.paint(g * m, col, spec=0.2, add=-0.5 if letter == 'N' else -0.9)
    # inner dark lip
    m = (r < 34) & (r >= 31.5)
    c.paint(m, hexrgb('#141819'), spec=0.3, shin=16, height=profile_round(34 - r, 2.5, 1.5))
    # gang socket lower right: a gang-coloured cup the icon from the .sty sits in
    sx, sy, sr = 88.0, 84.0, 15.5
    s = sd_circle(c, sx, sy, sr)
    c.paint(s < 0, hexrgb('#1d2224'), spec=0.4, shin=20, height=np.maximum(c.h_, profile_bevel(-s, 2.0, 5.0)))
    ring = (s < 0) & (s > -3.0)
    c.paint(ring, gang_rgb, spec=0.9, shin=30, height=5.0 + profile_round(-s, 3.0, 1.6))
    inner = s < -3.0
    c.paint(inner, hexrgb('#101314'), height=3.5)
    c.alpha[(r < 47) | (s < 0)] = 1
    c.alpha[r < 31.5] = 0
    d = Design()
    d.name = 'D_compass_dial'; d.title = 'D  Compass dial + gang socket'
    d.canvas = c; d.W, d.H = W, H
    d.rgb, _ = shade(c)
    d.screen_sd = r - 31.5
    d.screen_box = (cx - 31.5, cy - 31.5, cx + 31.5, cy + 31.5)
    d.center = (cx, cy)
    d.socket = (sx, sy, sr - 3.0)
    c.sw, c.sh = 63, 63
    d.glass = _glass(c, d.screen_sd, cx, cy)
    d.map_style = 'colour'
    d.rotates = True
    return d


def player_arrow():
    """Player marker: a chunky bevelled chevron, pale yellow plastic with a dark rim."""
    W, H = 11, 13
    c = Canvas(W, H)
    sd = sd_poly(c, [(5.5, 0.4), (10.6, 12.6), (5.5, 9.4), (0.4, 12.6)])
    c.paint(sd < 0, hexrgb('#1a1408'), height=0.5)
    inner = sd < -1.0
    c.paint(inner, hexrgb('#fff4a0'), spec=0.8, shin=28, height=0.5 + profile_bevel(-sd - 1.0, 1.4, 1.8))
    c.alpha[sd < 0] = 1
    rgb, _ = shade(c, ambient=0.60, diffuse=0.8, ao_r=1.0, ao_k=0.2)
    return c, rgb



def _falloff(c, rgb, k=0.18):
    """Key light falloff across the object, brighter at the top left, as in a point-lit pre-render."""
    t = (c.x / c.w + c.y / c.h) / 2
    return np.clip(rgb * (1 + k / 2 - k * t)[..., None], 0, 1)


def _tube(c, m, across, along, radius, colour, period=3.5, core_k=0.45):
    """Respect-meter glass tube along a path: `across` = distance from its centre line, `along` = arc length."""
    rib = 0.72 + 0.28 * (0.5 + 0.5 * np.cos(along * 2 * np.pi / period)) ** 0.6
    c.paint(m, colour, spec=1.1, shin=40,
            height=1.0 + profile_round(radius - across, 2 * radius, radius * 0.9) + 0.12 * np.cos(along * 2 * np.pi / period))
    c.alb[m] *= rib[m][:, None]
    core = np.clip(1 - across / (radius * 0.42), 0, 1) * m
    c.emit += (core * rib)[..., None] * np.array(colour) / 255.0 * core_k


def tube_frame(tube_rgb=hexrgb('#8a4a12')):
    """E: four straight respect-meter tubes in a bronze housing, joined by rubber corner caps."""
    W, H = 104, 92
    c = Canvas(W, H)
    bronze = hexrgb('#86704c'); bronze_dk = hexrgb('#4a3a26')
    house = sd_rrect(c, 1, 1, 103, 91, 6)
    c.paint(house < 0, bronze, spec=0.35, shin=16, height=profile_bevel(-house, 2.0, 2.5))
    inner = sd_rrect(c, 13, 13, 91, 79, 3)
    lip = (inner >= 0) & (inner < 1.6)
    c.paint(lip, bronze, spec=0.35, shin=16, height=2.5 + profile_round(inner, 1.6, 0.8))
    T, Bt, L, R, rad = 7.0, 85.0, 7.0, 97.0, 4.6
    groove = (house < 0) & (((np.abs(c.y - T) < rad + 1) | (np.abs(c.y - Bt) < rad + 1)) & (c.x > L) & (c.x < R) |
                            ((np.abs(c.x - L) < rad + 1) | (np.abs(c.x - R) < rad + 1)) & (c.y > T) & (c.y < Bt))
    c.paint(groove, bronze_dk, height=0.6)
    for yc in (T, Bt):
        m = (np.abs(c.y - yc) < rad) & (c.x > L) & (c.x < R)
        _tube(c, m, np.abs(c.y - yc), c.x, rad, tube_rgb)
    for xc in (L, R):
        m = (np.abs(c.x - xc) < rad) & (c.y > T) & (c.y < Bt)
        _tube(c, m, np.abs(c.x - xc), c.y, rad, tube_rgb)
    # N cap in the top tube, brighter tube colour with a dark engraved N
    cap = (np.abs(c.y - T) < rad) & (np.abs(c.x - 52) < 7)
    c.paint(cap, tuple(min(255, v * 2.1) for v in tube_rgb), spec=0.9, shin=30,
            height=1.3 + profile_round(rad - np.abs(c.y - T), 2 * rad, rad * 0.9))
    c.emit[cap] *= 0
    c.paint(cap & (np.abs(np.abs(c.x - 52) - 7) < 0.45), bronze_dk, height=1.0)
    g = glyph_mask(c, GLYPHS['N'], 52 - 2.5, T - 2.5, 1.0) * cap
    c.paint(g, hexrgb('#1a0c04'), add=-1.3)
    # rubber corner caps with a bronze rivet
    for x0, y0 in ((L, T), (R, T), (L, Bt), (R, Bt)):
        k = sd_rrect(c, x0 - 7, y0 - 7, x0 + 7, y0 + 7, 4)
        c.paint(k < 0, hexrgb('#3a332c'), spec=0.45, shin=14, height=profile_round(np.minimum(-k, 3.5), 7.0, 6.0) + np.where(-k > 3.5, 6.0, 0))
        rv = sd_circle(c, x0, y0, 2.0)
        c.paint(rv < 0, bronze, spec=0.8, shin=30, height=6.0 + profile_round(-rv, 2.0, 0.9))
    c.alpha[house < 0] = 1
    screen = sd_rrect(c, 13, 13, 91, 79, 3)
    c.alpha[screen < 0] = 0
    d = Design()
    d.name = 'E_tube_frame'; d.title = 'E  Tube frame'
    d.canvas = c; d.W, d.H = W, H
    d.rgb = _falloff(c, shade(c)[0])
    d.screen_sd = screen; d.screen_box = (13, 13, 91, 79); d.center = (52, 46)
    c.sw, c.sh = 78, 66
    d.glass = _glass(c, screen, 52, 46, 'rect')
    d.map_style = 'schematic'; d.rotates = False
    return d


def police_scanner():
    """F: a police radar scanner: navy plastic housing, red and blue light bar, curved CRT with a phosphor sweep."""
    W, H = 104, 100
    c = Canvas(W, H)
    body = sd_rrect(c, 1, 8, 103, 99, 12)
    c.paint(body < 0, hexrgb('#283444'), spec=0.4, shin=14, height=profile_round(np.minimum(-body, 7.0), 14.0, 6.0) + np.where(-body > 7, 6.0, 0))
    bar = sd_rrect(c, 28, 0.5, 76, 13, 5)
    bm = (bar < 0) & (body > -4)
    c.paint(bm, hexrgb('#14181e'), spec=0.4, shin=14, height=profile_bevel(-bar, 2.0, 7.0))
    for x0, col, glow in ((41, '#e01810', (0.55, 0.06, 0.04)), (63, '#1848f0', (0.05, 0.12, 0.6))):
        dome = sd_rrect(c, x0 - 9, 2.5, x0 + 9, 10.5, 4)
        c.paint(dome < 0, hexrgb(col), spec=1.0, shin=40, height=7.0 + profile_round(-dome, 4.0, 2.2))
        c.emit[dome < 0] += np.array(glow)
        ribs = (dome < -0.8) & (np.abs(((c.x - x0) / 2.0) % 1 - 0.5) < 0.12)
        c.alb[ribs] *= 0.7
    # recessed CRT well
    well = sd_rrect(c, 8, 16, 96, 92, 9)
    c.paint(well < 0, hexrgb('#0e1218'), spec=0.3, shin=16, height=6.0 - 4.0 * np.clip(-well / 3.5, 0, 1))
    for kx in (14, 90):
        k = sd_circle(c, kx, 95.5, 2.4)
        c.paint(k < 0, hexrgb('#9aa2aa'), spec=0.9, shin=30, height=6.0 + profile_round(-k, 2.4, 1.2))
    c.alpha[body < 0] = 1; c.alpha[bm] = 1
    screen = sd_rrect(c, 12, 20, 92, 88, 8)
    c.alpha[screen < 0] = 0
    d = Design()
    d.name = 'F_police_scanner'; d.title = 'F  Police scanner CRT'
    d.canvas = c; d.W, d.H = W, H
    d.rgb = _falloff(c, shade(c)[0])
    d.screen_sd = screen; d.screen_box = (12, 20, 92, 88); d.center = (52, 54)
    c.sw, c.sh = 80, 68
    d.glass = _glass(c, screen, 52, 54, 'rect', sheen_k=0.30, shadow_k=0.45, edge_k=0.55)
    d.map_style = 'phosphor'; d.rotates = True
    return d


def dash_gauge():
    """G: a car dashboard gauge pod: dark hood, chrome cone bezel, black dial face with ticks, N pointer."""
    W, H = 100, 104
    c = Canvas(W, H)
    cx, cy = 50.0, 56.0
    r = np.hypot(c.x - cx, c.y - cy); ang = np.arctan2(c.y - cy, c.x - cx)
    pod = sd_circle(c, cx, cy, 47.5)
    hood = sd_rrect(c, 6, 0.5, 94, 44, 14)
    shell = np.minimum(pod, hood)
    c.paint(shell < 0, hexrgb('#1c1e20'), spec=0.3, shin=12, height=profile_bevel(-shell, 3.0, 4.0))
    hm = (hood < 0) & (c.y < cy - 20)
    c.h_[hm] = np.maximum(c.h_[hm], (4.0 + 3.0 * profile_bevel(-hood, 4.0, 1.0))[hm])
    # chrome cone bezel
    m = (r < 44) & (r >= 36)
    c.paint(m, hexrgb('#a0a8ae'), spec=1.0, shin=36, height=4.0 + 3.5 * np.clip((44 - r) / 8.0, 0, 1) ** 0.7)
    lipm = (r < 45.2) & (r >= 44)
    c.paint(lipm, hexrgb('#5a6066'), spec=0.6, shin=20, height=4.2)
    # dial face with printed ticks and the N pointer
    m = (r < 36) & (r >= 30.5)
    c.paint(m, hexrgb('#0d0e10'), spec=0.5, shin=40, height=2.5)
    step = np.pi / 12
    t = np.abs(((ang + np.pi / 2) / step + 0.5) % 1 - 0.5) * step * r
    major = np.abs(((ang + np.pi / 2) / (np.pi / 4) + 0.5) % 1 - 0.5) * (np.pi / 4) * r
    tick = m & (((t < 0.55) & (r > 32.6)) | ((major < 0.8) & (r > 31.0)))
    c.paint(tick, hexrgb('#e8e4d8'), spec=0.5, shin=40, height=2.5)
    ptr = sd_poly(c, [(cx, cy - 30.6), (cx + 3.2, cy - 36.5), (cx - 3.2, cy - 36.5)])
    c.paint(ptr < 0, hexrgb('#ff3a00'), spec=0.15, shin=40, height=2.5)
    c.emit[ptr < 0] += np.array([0.6, 0.15, 0.0])
    g = glyph_mask(c, GLYPHS['N'], cx - 2.5, 4.5, 1.0) * (hood < 0)
    c.paint(g, hexrgb('#f0ece0'), spec=0.2, add=0.0)
    c.alpha[shell < 0] = 1
    c.alpha[r < 30.5] = 0
    d = Design()
    d.name = 'G_dash_gauge'; d.title = 'G  Dashboard gauge'
    d.canvas = c; d.W, d.H = W, H
    d.rgb = _falloff(c, shade(c)[0])
    d.screen_sd = r - 30.5; d.screen_box = (cx - 30.5, cy - 30.5, cx + 30.5, cy + 30.5); d.center = (cx, cy)
    c.sw, c.sh = 61, 61
    rgbg, ag = _glass(c, d.screen_sd, cx, cy, sheen_k=0.30, shadow_k=0.65, edge_k=0.45)
    # the hood darkens the top of the glass
    hood_sh = np.clip((cy - 8 - c.y) / 22.0, 0, 1) * 0.35 * (d.screen_sd < 0)
    d.glass = (rgbg * (ag / np.maximum(ag + hood_sh, 1e-4))[..., None], np.clip(ag + hood_sh, 0, 0.85))
    d.map_style = 'night'; d.rotates = True
    return d


def gps_handheld():
    """H: a chunky 90s handheld GPS in the frontend's yellow and black, rubber bumper, antenna stub, keys."""
    W, H = 96, 110
    c = Canvas(W, H)
    body = sd_rrect(c, 2, 10, 94, 108, 16)
    c.paint(body < 0, hexrgb('#e8b414'), spec=0.30, shin=10, height=profile_round(np.minimum(-body, 8.0), 16.0, 7.0) + np.where(-body > 8, 7.0, 0))
    ant = sd_rrect(c, 9, 0.5, 23, 18, 6)
    am = (ant < 0) & (body > -5)
    c.paint(am, hexrgb('#1a1a1a'), spec=0.4, shin=14, height=profile_round(np.minimum(-ant, 7.0), 14.0, 8.0))
    for yy in (4.0, 7.5):
        c.h_[am & (np.abs(c.y - yy) < 0.5)] -= 1.0
    face = sd_rrect(c, 9, 18, 87, 101, 10)
    c.paint(face < 0, hexrgb('#141414'), spec=0.35, shin=18, height=7.0 - 1.5 * np.clip(-face / 1.5, 0, 1))
    bez = sd_rrect(c, 13, 24.5, 83, 82, 5)
    c.paint(bez < 0, hexrgb('#4a4c46'), spec=0.5, shin=22, height=5.5 - 1.5 * np.clip(-bez / 2.0, 0, 1))
    g = glyph_mask(c, GLYPHS['N'], 48 - 2.5, 19.0, 1.0) * (face < 0)
    c.paint(g, hexrgb('#e8b414'), add=0.0)
    for bx, col in ((30, '#2a2a2a'), (48, '#d02818'), (66, '#2a2a2a')):
        b = sd_circle(c, bx, 91.5, 5.2)
        c.paint(b < 0, hexrgb(col), spec=0.6, shin=24, height=5.5 + profile_round(-b, 4.6, 2.0))
    for bx, gl in ((30, ["#####", ".....", "....."]), (66, ["..#..", "#####", "..#.."])):
        m = glyph_mask(c, gl, bx - 2.5, 90.5 + (1 if bx == 30 else -0.0), 1.0)
        c.paint(m * (c.y < 94), hexrgb('#e8b414'), add=0.0)
    c.alpha[body < 0] = 1; c.alpha[am] = 1
    screen = sd_rrect(c, 16, 27.5, 80, 79, 3)
    c.alpha[screen < 0] = 0
    d = Design()
    d.name = 'H_gps_handheld'; d.title = 'H  90s handheld GPS'
    d.canvas = c; d.W, d.H = W, H
    d.rgb = _falloff(c, shade(c)[0])
    d.screen_sd = screen; d.screen_box = (16, 27.5, 80, 79); d.center = (48, 53.25)
    c.sw, c.sh = 64, 51.5
    d.glass = _glass(c, screen, 48, 53.25, 'rect')
    d.map_style = 'gps'; d.rotates = False
    return d


BUILDERS = [respect_ring, pager_handset, steel_plate, compass_dial]
BUILDERS2 = [tube_frame, police_scanner, dash_gauge, gps_handheld]
