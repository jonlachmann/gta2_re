# Minimal GTA2 .sty reader: sprites with their palettes, as RGBA images.
import struct
from PIL import Image

class Sty:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert d[:4] == b'GBST'
        self.chunks = {}
        p = 6
        while p + 8 <= len(d):
            t = d[p:p+4].decode('latin1'); n = struct.unpack_from('<I', d, p+4)[0]
            self.chunks[t] = d[p+8:p+8+n]; p += 8 + n
        c = self.chunks
        self.palx = struct.unpack('<%dH' % (len(c['PALX'])//2), c['PALX'])
        self.ppal = c['PPAL']
        self.palb = struct.unpack_from('<8H', c['PALB'])
        self.sprb = struct.unpack_from('<6H', c['SPRB'])
        self.sprx = [struct.unpack_from('<IBBH', c['SPRX'], i*8) for i in range(len(c['SPRX'])//8)]
        self.sprg = c['SPRG']

    def colour(self, phys, idx):
        page = phys // 64
        o = page * 65536 + (idx * 64 + phys % 64) * 4
        b, g, r, a = self.ppal[o:o+4]
        return (r, g, b)

    def user_base(self):
        return sum(self.sprb[:4])

    def sprite(self, n, vpal=None):
        ptr, w, h, _ = self.sprx[n]
        if vpal is None:
            vpal = self.palb[0] + n
        phys = self.palx[vpal]
        img = Image.new('RGBA', (w, h))
        page = ptr // 65536; x0 = ptr % 256; y0 = (ptr // 256) % 256
        for y in range(h):
            for x in range(w):
                v = self.sprg[page*65536 + (y0+y)*256 + x0 + x]
                if v:
                    img.putpixel((x, y), self.colour(phys, v) + (255,))
        return img
