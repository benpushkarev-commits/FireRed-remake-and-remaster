import sys, os
from PIL import Image, ImageDraw
src, out = sys.argv[1], sys.argv[2]
names = sorted(f for f in os.listdir(src) if f.endswith('.png'))
if len(sys.argv) > 3:
    names = [n for n in names if any(k in n for k in sys.argv[3].split(','))]
S, cols = 160, 8
rows = (len(names) + cols - 1) // cols
sheet = Image.new('RGB', (cols * S, rows * (S + 14)), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    im = Image.open(os.path.join(src, n)).convert('RGB')
    im.thumbnail((S, S))
    x, y = (i % cols) * S, (i // cols) * (S + 14)
    sheet.paste(im, (x, y))
    dr.text((x + 2, y + S), n[:26], fill=(255, 255, 255))
sheet.save(out)
print(len(names), sheet.size)
