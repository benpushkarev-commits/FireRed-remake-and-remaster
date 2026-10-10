"""Recover gfpak file names: FNV-1a 64 of names listed in the pack's own config strings."""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak

def fnv1a64(s):
    h = 0xcbf29ce484222325
    for c in s.encode():
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def names(path):
    files = gfpak.read(path)
    cands = set()
    for _, d in files:
        for m in re.finditer(rb'[\x20-\x7e]{4,}', d):
            s = m.group().decode()
            cands.add(s)
    base = os.path.basename(path)[:-6]
    extra = set()
    for s in cands:
        for ext in ('', '.gfbmdl', '.gfbanm', '.gfbanmcfg', '.bntx', '.bnsh'):
            extra.add(s + ext)
    for e in ('.gfbmdl', '.gfbanmcfg', '_fi.gfbanmcfg', '_ba.gfbanmcfg'):
        extra.add(base + e)
    table = {fnv1a64(s): s for s in extra}
    out = []
    for i, (h, d) in enumerate(files):
        out.append((i, h, table.get(h), d))
    return out

if __name__ == '__main__':
    for i, h, n, d in names(sys.argv[1]):
        print(i, '%016x' % h, n, len(d), d[:4].hex())
