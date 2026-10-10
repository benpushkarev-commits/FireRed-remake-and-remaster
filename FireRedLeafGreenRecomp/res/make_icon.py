# Generates firered_remaster.ico: an original pixel-art flame on a rounded badge.
import math
from PIL import Image

def flame_field(u, v):
    """Signed 'inside-ness' for a 3-tongue flame. u in [-1,1], v in [0,1] (0 = top)."""
    def tongue(u, v, cx, top, width, lean):
        if v < top: return -1.0
        t = (v - top) / (1.0 - top)                       # 0 at tip, 1 at base
        hw = width * math.sin(0.5 * math.pi * min(1.0, t / 0.72)) ** 1.7
        if t > 0.80: hw *= math.sqrt(max(0.0, 1 - ((t - 0.80) / 0.20) ** 2))
        x = u - cx - lean * (1 - t) ** 1.6
        return (hw - abs(x)) / max(width, 1e-6)
    a = tongue(u, v, 0.00, 0.00, 0.78, 0.22)
    b = tongue(u, v, -0.40, 0.34, 0.46, -0.42)
    c = tongue(u, v, 0.42, 0.42, 0.42, 0.38)
    return max(a, b, c)

def render(size):
    S = 8  # supersample for shape decisions, then pick per pixel
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    # badge
    r = max(2, round(size * 0.19))
    def in_badge(x, y):
        cx = min(max(x, r), size - 1 - r); cy = min(max(y, r), size - 1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r * 0.5
    top_c, bot_c = (122, 26, 18), (52, 10, 10)
    for y in range(size):
        for x in range(size):
            if not in_badge(x, y): continue
            t = y / (size - 1)
            col = tuple(round(top_c[i] * (1 - t) + bot_c[i] * t) for i in range(3))
            edge = not (in_badge(x - 1, y) and in_badge(x + 1, y) and in_badge(x, y - 1) and in_badge(x, y + 1))
            if edge: col = (28, 4, 4)
            elif not in_badge(x, y - 1 - max(0, size // 32)) and y < size // 2: col = (196, 70, 44)  # top highlight
            px[x, y] = col + (255,)
    # flame bands
    bands = [(0.00, (224, 56, 24)), (0.30, (248, 136, 40)), (0.56, (248, 208, 64)), (0.80, (255, 246, 196))]
    margin = size * 0.13
    fw, fh = size - 2 * margin, size - 2 * margin
    level = [[-1] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            u = ((x + 0.5) - size / 2) / (fw / 2)
            v = ((y + 0.5) - margin * 0.80) / fh
            if v < 0 or v > 1: continue
            f = flame_field(u, v)
            if f <= 0: continue
            # inner bands are the same shape shrunk toward the base centre
            lv = 0
            for i, (th, _) in enumerate(bands):
                uu = u / (1 - th * 0.80) if th else u
                vv = 1 - (1 - v) / (1 - th * 0.62) if th else v
                if th == 0 or (0 <= vv <= 1 and flame_field(uu, vv) > 0.05):
                    lv = i
            level[y][x] = lv
    for y in range(size):
        for x in range(size):
            lv = level[y][x]
            if lv < 0:
                # outline: any flame neighbour
                if any(0 <= x + dx < size and 0 <= y + dy < size and level[y + dy][x + dx] >= 0
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    px[x, y] = (36, 6, 2, 255)
                continue
            px[x, y] = bands[lv][1] + (255,)
    return img

base32 = render(32)
base16 = render(16)
base48 = render(48)
big = {s: base32.resize((s, s), Image.NEAREST) for s in (64, 128, 256)}
base32.resize((256, 256), Image.NEAREST).save("icon_preview.png")
imgs = [base16, base32, base48, big[64], big[128], big[256]]
big[256].save("firered_remaster.ico", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
              append_images=imgs[:-1])
# also a strip preview of the hand sizes
strip = Image.new("RGBA", (16 * 8 + 32 * 8 + 48 * 4 + 40, 48 * 4), (240, 240, 240, 255))
strip.paste(base16.resize((128, 128), Image.NEAREST), (0, 0), base16.resize((128, 128), Image.NEAREST))
strip.paste(base32.resize((256, 256), Image.NEAREST).resize((192, 192), Image.NEAREST), (140, 0))
strip.paste(base48.resize((192, 192), Image.NEAREST), (350, 0))
strip.save("icon_sizes.png")
print("ok")
