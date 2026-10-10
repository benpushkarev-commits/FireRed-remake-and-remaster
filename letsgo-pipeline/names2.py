import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xxhash
def fnv1a64(s):  # GFLX packs hash names with xxHash64 (seed 0)
    return xxhash.xxh64_intdigest(s.encode())
import gfpak
p = sys.argv[1]
d = open(p, 'rb').read()
ver, _, nfiles, nfolders, info_off, hash_off, folder_off = struct.unpack_from('<IIIIQQQ', d, 8)
files = gfpak.read(p)
cands = set()
for _, b in files:
    for m in re.finditer(rb'[\x20-\x7e]{4,}', b):
        cands.add(m.group().decode())
table = {}
for s in cands:
    for ext in ('', '.gfbmdl', '.gfbanm', '.gfbanmcfg', '.bntx'):
        table[fnv1a64(s + ext)] = s + ext
base = os.path.basename(p)[:-6]
for e in ('.gfbmdl', '.gfbanmcfg', '_fi.gfbanmcfg', '_ba.gfbanmcfg', '_ev.gfbanmcfg'):
    table[fnv1a64(base + e)] = base + e
fo = folder_off
for f in range(nfolders):
    fh, cnt, _ = struct.unpack_from('<QII', d, fo)
    print('folder %016x' % fh, cnt)
    for k in range(cnt):
        h, idx, _ = struct.unpack_from('<QII', d, fo + 16 + 16 * k)
        print('  %3d %016x %s %d' % (idx, h, table.get(h), len(files[idx][1])))
    fo += 16 + 16 * cnt
