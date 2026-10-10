import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, gfbanm, gfbmdl
files = gfpak.read(sys.argv[1])
mdl = max((d for _, d in files if d[:4] == b'\x20\x00\x00\x00'), key=len)
m = gfbmdl.load(mdl)
print('bones', len(m['bones']), [b['name'] for b in m['bones']][:40])
for i, (h, d) in enumerate(files):
    if d[:4] != b'\x04\x00\x00\x00': continue
    try:
        a = gfbanm.load(d)
    except Exception as e:
        print(i, 'ERR', e); continue
    def motion(name):
        b = a['bones'].get(name)
        if not b or not b['rot']: return 0
        qs = [q for _, q in b['rot']]
        return max(abs(qs[k][j] - qs[0][j]) for k in range(len(qs)) for j in range(4))
    hips = None
    for n in ('Waist', 'Hips', 'waist', 'Spine1', 'Pelvis'):
        if n in a['bones']: hips = n; break
    tr = a['bones'].get('Origin', {}).get('trans') or a['bones'].get('Waist', {}).get('trans')
    dz = (tr[-1][1][2] - tr[0][1][2]) if tr else 0
    legs = [n for n in a['bones'] if 'Leg' in n or 'leg' in n or 'Thigh' in n]
    print(i, 'frames', a['frames'], 'fps', a['fps'], 'bones', len(a['bones']), 'legmove %.2f' % max([motion(n) for n in legs] or [0]),
          'hips', hips, 'dz %.1f' % dz, sorted(a['bones'])[:6])
