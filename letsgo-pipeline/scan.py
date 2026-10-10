import os, sys, collections
sys.path.insert(0, os.path.dirname(__file__))
import gfpak
root = sys.argv[1]
loose = collections.Counter()
for dp, _, fs in os.walk(root):
    for f in fs:
        p = os.path.join(dp, f)
        loose[os.path.splitext(f)[1]] += 1
        if f.endswith('.gfpak') and os.path.getsize(p) < 200_000_000:
            try:
                mags = collections.Counter(d[:4] for _, d in gfpak.read(p))
            except Exception as e:
                print('ERR', p, e); continue
            if b'BNTX' in mags:
                print(os.path.relpath(p, root), dict((k.decode('latin1'), v) for k, v in mags.items()))
print(loose.most_common(40))
