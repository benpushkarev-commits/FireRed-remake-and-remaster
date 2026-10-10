"""Recover .gfbanm names inside a Let's Go pack.

File-name hashes in the pack are not reversible, but every animation name appears in the
pack's .gfbanmcfg strings, and the animation files are stored in name order. The eye/mouth
blend animations drive only a few face bones, which anchors the alignment when a pack holds
extra unnamed clips at the front."""
import re
import gfbanm

def names_and_files(files):
    names = set()
    for _, d in files:
        if d[:4] == b'\x18\x00\x00\x00':
            for m in re.finditer(rb'([A-Za-z0-9_]+)\.gfbanm\x00', d):
                names.add(m.group(1).decode())
    names = sorted(names)
    anims = []
    for i, (_, d) in enumerate(files):
        if d[:4] in (b'\x04\x00\x00\x00', b'\x14\x00\x00\x00'):
            try:
                a = gfbanm.load(d)
            except Exception:
                continue
            if a['frames'] > 0 and a['bones']:
                anims.append((i, a))
    return names, anims

def resolve(files):
    """Return {short clip name (prefix stripped): parsed animation}."""
    names, anims = names_and_files(files)
    if not names or len(anims) < len(names):
        return {}
    most = max(len(a['bones']) for _, a in anims)
    few = [len(a['bones']) < most * 0.3 for _, a in anims]
    face = [('eye' in n or 'mouth' in n) for n in names]
    best, best_score = 0, -1
    for shift in range(len(anims) - len(names) + 1):
        score = sum(1 for k, f in enumerate(face) if f and few[shift + k]) - sum(1 for k, f in enumerate(face) if few[shift + k] and not f)
        if score > best_score:
            best, best_score = shift, score
    out = {}
    for k, n in enumerate(names):
        short = n.split('_', 2)[2] if n.count('_') >= 2 else n   # pm0016_00_ba10_waitA01 -> ba10_waitA01
        out[short] = anims[best + k][1]
    return out

def pick(clips, *patterns):
    """First clip whose short name contains any of the patterns, in priority order."""
    for p in patterns:
        for n in sorted(clips):
            if p in n:
                return clips[n]
    return None
