# Lays out chosen extracted sprites enlarged with nearest-neighbour, for study only.
# usage: python3 -I zoomref.py <dir of user_NNN.png> <out.png> <zoom> <ids csv>
import sys
from PIL import Image
d, out, z = sys.argv[1], sys.argv[2], int(sys.argv[3]); ids = [int(x) for x in sys.argv[4].split(',')]
ims = [Image.open('%s/user_%03d.png' % (d, i)) for i in ids]
W = sum(im.width*z + 8 for im in ims); H = max(im.height for im in ims)*z
c = Image.new('RGBA', (W, H), (90, 90, 100, 255)); x = 0
for im in ims:
    c.alpha_composite(im.resize((im.width*z, im.height*z), Image.NEAREST), (x, 0)); x += im.width*z + 8
c.save(out)
