#!/usr/bin/env python3
"""Render FireRed metatiles straight from the ROM, labelled with their hex ids.

  python tools/py/tileset_sheet.py ROM OUT.bmp --layout PalletTown_Layout [Route1_Layout ...]
      every metatile those maps use (tilesets taken from the first layout)
  python tools/py/tileset_sheet.py ROM OUT.bmp --tilesets gTileset_General gTileset_PalletTown FIRST COUNT
      a range of ids
  python tools/py/tileset_sheet.py ROM OUT.bmp --map Route1_Layout X Y W H
      that map area in place, every cell labelled

Prints "id behavior layer uses collision" per tile (feeds the 3D model table).
"""
import pathlib, struct, sys
from collections import Counter

root = pathlib.Path(__file__).resolve().parents[2]
syms = {}
for line in open(root / "FireRedLeafGreenRecomp/variants/firered/symbols/all_symbols.tsv", encoding="utf-8"):
    if line.startswith("#"): continue
    f = line.rstrip("\n").split("\t")
    if len(f) >= 6: syms[f[5]] = int(f[0], 16)

rom = open(sys.argv[1], "rb").read()
def off(a): return a - 0x08000000
def u32(a): return struct.unpack_from("<I", rom, off(a))[0]
def lz77(a):
    o = off(a); assert rom[o] == 0x10
    size = rom[o + 1] | rom[o + 2] << 8 | rom[o + 3] << 16
    out, o = bytearray(), o + 4
    while len(out) < size:
        flags = rom[o]; o += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                b1, b2 = rom[o], rom[o + 1]; o += 2
                for _ in range((b1 >> 4) + 3): out.append(out[-(((b1 & 15) << 8 | b2) + 1)])
            else:
                out.append(rom[o]); o += 1
    return bytes(out)
def tileset(addr):
    tiles_p, pals_p, meta_p, _, attr_p = struct.unpack_from("<IIIII", rom, off(addr) + 4)
    tiles = lz77(tiles_p) if rom[off(addr)] else rom[off(tiles_p):off(tiles_p) + 0x8000]
    return tiles, pals_p, meta_p, attr_p

out_path, mode = sys.argv[2], sys.argv[3]
uses, coll = Counter(), {}
if mode == "--layout":
    layouts = [syms[n] for n in sys.argv[4:]]
    prim, sec = u32(layouts[0] + 16), u32(layouts[0] + 20)
    for lay in layouts:
        w, h, data = u32(lay), u32(lay + 4), u32(lay + 12)
        for i in range(w * h):
            v = struct.unpack_from("<H", rom, off(data) + i * 2)[0]
            uses[v & 0x3ff] += 1
            coll.setdefault(v & 0x3ff, set()).add((v >> 10) & 3)
    ids = sorted(uses)
elif mode == "--map":
    lay = syms[sys.argv[4]]
    prim, sec = u32(lay + 16), u32(lay + 20)
    mw, data = u32(lay), u32(lay + 12)
    mx, my, cols, mh = map(int, sys.argv[5:9])
    ids = []
    for y in range(my, my + mh):
        for x in range(mx, mx + cols):
            v = struct.unpack_from("<H", rom, off(data) + (y * mw + x) * 2)[0]
            ids.append(v & 0x3ff); uses[v & 0x3ff] += 1
            coll.setdefault(v & 0x3ff, set()).add((v >> 10) & 3)
else:
    prim, sec = syms[sys.argv[4]], syms[sys.argv[5]]
    ids = list(range(int(sys.argv[6]), int(sys.argv[6]) + int(sys.argv[7])))

pt, ppal, pmeta, pattr = tileset(prim)
st, spal, smeta, sattr = tileset(sec)
vram = bytearray(1024 * 32)
vram[:min(len(pt), 640 * 32)] = pt[:640 * 32]
vram[640 * 32:640 * 32 + min(len(st), 384 * 32)] = st[:384 * 32]
pal = []
for i in range(16):
    base = ppal if i < 7 else spal
    for c in range(16):
        v = struct.unpack_from("<H", rom, off(base) + (i * 16 + c) * 2)[0]
        pal.append(((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31))

FONT = {  # 3x5 hex digits
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
    "8": "111101111101111", "9": "111101111001111", "a": "111101111101101", "b": "110101110101110",
    "c": "111100100100111", "d": "110101101101110", "e": "111100111100111", "f": "111100111100100"}
S, LAB = 3, 7
if mode != "--map": cols = 16
CW, CH = 16 * S + 2, 16 * S + LAB + 2
rows = (len(ids) + cols - 1) // cols
W, H = cols * CW, rows * CH
img = bytearray([30, 30, 30] * W * H)
def put(x, y, c):
    o = (y * W + x) * 3; img[o:o + 3] = bytes((c[2], c[1], c[0]))
for n, mid in enumerate(ids):
    meta, attr, idx = (pmeta, pattr, mid) if mid < 640 else (smeta, sattr, mid - 640)
    ents = struct.unpack_from("<8H", rom, off(meta) + idx * 16)
    a = u32(attr + idx * 4)
    print("%03x b%02x l%d n%d c%s" % (mid, a & 0x1ff, (a >> 29) & 3, uses[mid], "".join(map(str, sorted(coll.get(mid, []))))))
    px = [pal[0]] * 256
    for layer in range(2):
        for q in range(4):
            e = ents[layer * 4 + q]
            t, p = e & 1023, (e >> 12) * 16
            for y in range(8):
                for x in range(8):
                    sx, sy = (7 - x if e & 0x400 else x), (7 - y if e & 0x800 else y)
                    ci = (vram[t * 32 + sy * 4 + sx // 2] >> ((sx & 1) * 4)) & 15
                    if ci: px[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = pal[p + ci]
    ox, oy = (n % cols) * CW + 1, (n // cols) * CH + 1
    for y in range(16 * S):
        for x in range(16 * S): put(ox + x, oy + y, px[(y // S) * 16 + x // S])
    for k, ch in enumerate("%03x" % mid):
        for gy in range(5):
            for gx in range(3):
                if FONT[ch][gy * 3 + gx] == "1": put(ox + 14 + k * 4 + gx, oy + 16 * S + 1 + gy, (255, 255, 120))
with open(out_path, "wb") as f:
    pad = (4 - W * 3 % 4) % 4
    f.write(b"BM" + struct.pack("<IHHI", 54 + (W * 3 + pad) * H, 0, 0, 54))
    f.write(struct.pack("<IiiHHIIiiII", 40, W, -H, 1, 24, 0, (W * 3 + pad) * H, 0, 0, 0, 0))
    for y in range(H): f.write(img[y * W * 3:(y + 1) * W * 3] + b"\0" * pad)
