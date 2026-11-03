# Renders a top-down of a .gmp from its lid tiles (the user's .sty), with height shading. Output stays local.
# usage: python3 -I render_map.py <map.gmp> <style.sty> <out.png> [px per block]
import os, sys, struct
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sty import Sty

gmp, styp, out = sys.argv[1:4]; PX = int(sys.argv[4]) if len(sys.argv) > 4 else 8
d = open(gmp, 'rb').read(); p = 6; ch = {}
while p + 8 <= len(d):
    t = d[p:p+4].decode('latin1'); n = struct.unpack_from('<I', d, p+4)[0]; ch[t] = (p+8, n); p += 8 + n
o, n = ch['DMAP']
base = np.frombuffer(d, '<u4', 65536, o).reshape(256, 256); o += 65536*4
ncw = struct.unpack_from('<I', d, o)[0]; o += 4
cols = np.frombuffer(d, '<u4', ncw, o); o += ncw*4
nb = struct.unpack_from('<I', d, o)[0]; o += 4
blocks = np.frombuffer(d, np.dtype([('l','<u2'),('r','<u2'),('t','<u2'),('b','<u2'),('lid','<u2'),('ar','u1'),('sl','u1')]), nb, o)

st = Sty(styp); tiles = st.chunks['TILE']; ntiles = len(tiles) // 4096
pal = np.frombuffer(st.ppal, np.uint8).reshape(-1, 256, 64, 4)  # page, colour, palette-in-page, BGRA
def tile_rgb(n):
    pg = n // 16; tx = (n % 4)*64; ty = ((n // 4) % 4)*64
    a = np.frombuffer(tiles, np.uint8, 65536, pg*65536).reshape(256, 256)[ty:ty+64, tx:tx+64]
    ph = st.palx[n]; c = pal[ph // 64, :, ph % 64, :][:, [2, 1, 0]]
    return c[a].astype(np.float32), a == 0
cache = {}
def tile_small(n, rot, flip):
    k = (n, rot, flip)
    if k not in cache:
        rgb, tr = tile_rgb(n)
        im = Image.fromarray(rgb.astype(np.uint8)).resize((PX, PX), Image.BOX)
        a = np.asarray(im).astype(np.float32)
        if flip: a = a[:, ::-1]
        a = np.rot90(a, -rot)
        cache[k] = (a, tr.mean())
    return cache[k]

img = np.zeros((256*PX, 256*PX, 3), np.float32); hz = np.zeros((256, 256), np.float32); gt = np.zeros((256, 256), np.uint8)
for y in range(256):
    for x in range(256):
        ci = base[y, x]; w = cols[ci]; h = w & 0xFF; off = (w >> 8) & 0xFF
        bl = cols[ci+1:ci+1+(h-off)]
        col = None
        for z in range(len(bl)-1, -1, -1):
            b = blocks[bl[z]]; lid = int(b['lid']); t = lid & 0x3FF
            if t and t < ntiles:
                a, trf = tile_small(t, (lid >> 14) & 3, (lid >> 13) & 1)
                if trf > 0.6 and z > 0:
                    continue
                col = a; hz[y, x] = off + z; gt[y, x] = b['sl'] & 3; break
        if col is not None:
            img[y*PX:(y+1)*PX, x*PX:(x+1)*PX] = col
# height shading and a soft cast shadow toward the bottom right
sh = 0.80 + 0.045*np.clip(hz - 2, 0, 6)
cast = np.zeros_like(hz)
for k in (1, 2):
    cast = np.maximum(cast, np.clip(np.roll(np.roll(hz, k, 0), k, 1) - hz, 0, 3) / (3*k))
sh = sh * (1 - 0.35*np.clip(cast, 0, 1))
img *= np.kron(sh, np.ones((PX, PX)))[..., None]
Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(out)
np.save(out.replace('.png', '_h.npy'), hz); np.save(out.replace('.png', '_g.npy'), gt)
print('done', nb, 'blocks', ntiles, 'tiles')
