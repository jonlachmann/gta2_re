# Dumps the user (HUD) sprites of a .sty into a labelled contact sheet, for reference only (not shipped).
# usage: python3 -I extract_hud.py <style.sty> <out dir>
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sty import Sty
from PIL import Image, ImageDraw
st = Sty(sys.argv[1]); out = sys.argv[2]
base = st.user_base(); n_user = st.sprb[4]
print('sprb', st.sprb, 'user base', base)
cells = []
for i in range(n_user):
    im = st.sprite(base + i)
    im.save(os.path.join(out, 'user_%03d.png' % i))
    cells.append(im)
Z = 3; cw = 70*Z; ch = 70*Z + 14; cols = 10
rows = (len(cells) + cols - 1) // cols
sheet = Image.new('RGBA', (cols*cw, rows*ch), (90, 90, 100, 255))
d = ImageDraw.Draw(sheet)
for i, im in enumerate(cells):
    x, y = (i % cols)*cw, (i // cols)*ch
    s = min(Z, (cw-4) / max(1, im.width), (ch-18) / max(1, im.height))
    s = max(1, int(s))
    big = im.resize((im.width*s, im.height*s), Image.NEAREST)
    sheet.alpha_composite(big, (x+2, y+14))
    d.text((x+2, y+1), '%d %dx%d' % (i, im.width, im.height), fill=(255, 255, 0, 255))
sheet.save(os.path.join(out, 'user_sheet.png'))
