# Map-treatment variants on earlier frames: A and D with the high-contrast map of E.
# usage: python3 -I variants.py <data dir> <captures dir> <out dir>
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import make_all as M
M.setup(*sys.argv[1:4])
import designs
from mm_lib import downsample

cells = []
for build in (designs.respect_ring, designs.compass_dial):
    d = build(); d.map_style = 'schematic'
    out = os.path.join(M.OUT, d.name)
    for s, cap in ((1, '43_day'), (1.5, '169_dusk')):
        fr = downsample(d.rgb, d.canvas.alpha, s, 128)
        grgb, ga = d.glass
        gl = M.rgba_from(np.dstack([M.soft(grgb[..., i], s) for i in range(3)]), M.soft(ga, s))
        img, _ = M.place(M.CAPTURES[cap][0], s, M.compose(d, s, fr, gl))
        img.save(os.path.join(out, 'variant_schematic_composite_%s.png' % cap))
        if cap == '43_day':
            f = img.crop((496, 336, 640, 480)); f = f.resize((f.width * 3, f.height * 3), Image.NEAREST)
            f.save(os.path.join(out, 'variant_schematic_corner_x3.png'))
            cells.append(M.label(f, d.title + ' with the E map', 'map: high contrast by block kind'))
M.hstack(cells).save(os.path.join(M.OUT, 'variants_schematic.png'))
