# apply_icon.py <src.ico|png> — turn Ben's icon into res/firered_remaster.ico
# with every size Windows asks for (Explorer, taskbar, Alt-Tab, title bar).
import sys, shutil, os
from PIL import Image, IcoImagePlugin

src = sys.argv[1]
res = os.environ.get("ICON_RES", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "FireRedLeafGreenRecomp", "res"))
dst = os.path.join(res, "firered_remaster.ico")
keep = os.path.join(res, "firered_remaster_flame.ico")
if not os.path.exists(keep):
    shutil.copy(dst, keep)          # keep the old flame icon around

im = Image.open(src)
frames = {}
if im.format == "ICO":
    for size in sorted(im.info.get("sizes", {im.size}), key=lambda s: s[0] * s[1]):
        im.size = size
        im.load()
        frames[size[0]] = im.convert("RGBA").copy()
    print("source frames:", sorted(frames))
else:
    frames[max(im.size)] = im.convert("RGBA")
big = frames[max(frames)]
if big.width != big.height:   # pad to square, centred
    s = max(big.size)
    sq = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sq.paste(big, ((s - big.width) // 2, (s - big.height) // 2))
    big = sq

sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
out = []
for s in sizes:
    # use a hand-made frame when the source has that exact size
    f = frames.get(s)
    if f is None or f.size != (s, s):
        f = big.resize((s, s), Image.LANCZOS if big.width > s else Image.NEAREST)
    out.append(f)
out[-1].save(dst, format="ICO", sizes=[(s, s) for s in sizes], append_images=out[:-1])
# preview sheet
sheet = Image.new("RGBA", (sum(sizes) + 10 * len(sizes), 266), (40, 44, 52, 255))
x = 5
for f in out:
    sheet.alpha_composite(f, (x, 5))
    x += f.width + 10
sheet.save(os.path.join(res, "icon_sizes.png"))
print("wrote", dst, os.path.getsize(dst), "bytes")
