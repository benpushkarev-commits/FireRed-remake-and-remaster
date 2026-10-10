"""Overlay the FireRed tile grid / tile letters / props on a render_top.py image."""
import json, sys
from PIL import Image, ImageDraw
img, mapid, out = sys.argv[1], sys.argv[2], sys.argv[3]
w = json.load(open(r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\data\world.json', encoding='utf8'))
m = [v for v in w['maps'] if v['id'] == mapid][0]
im = Image.open(img).convert('RGB')
d = ImageDraw.Draw(im, 'RGBA')
T = 40
for z, row in enumerate(m['tiles']):
    for x, ch in enumerate(row):
        r = (x * T, z * T, x * T + T - 1, z * T + T - 1)
        d.rectangle(r, outline=(255, 255, 255, 60))
        if ch != '.':
            col = {'X': (255, 0, 0, 70), 'W': (0, 80, 255, 90), 'p': (255, 200, 0, 60)}.get(ch, (255, 0, 255, 60))
            d.rectangle(r, fill=col)
for p in m['props']:
    x, z = p['at']; sx, sz = p['size']
    d.rectangle((x * T, z * T, (x + sx) * T - 1, (z + sz) * T - 1), outline=(255, 255, 0, 255), width=3)
    if 'door' in p:
        dx, dz = p['door']
        d.rectangle((dx * T + 8, dz * T + 8, dx * T + T - 8, dz * T + T - 8), fill=(0, 255, 0, 200))
im.save(out)
