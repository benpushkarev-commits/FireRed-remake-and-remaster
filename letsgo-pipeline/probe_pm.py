import sys, os, math
sys.path.insert(0, r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo')
import gfpak, gfbanm, gfbmdl
files = gfpak.read(sys.argv[1])
for i, (h, d) in enumerate(files):
    if d[:4] not in (b'\x04\x00\x00\x00', b'\x14\x00\x00\x00'):
        continue
    try:
        a = gfbanm.load(d)
    except Exception as e:
        print(i, d[:4].hex(), 'ERR', e); continue
    o = a['bones'].get('Origin') or {}
    tr = o.get('trans')
    travel = 0
    if tr and len(tr) > 1:
        travel = math.hypot(tr[-1][1][0] - tr[0][1][0], tr[-1][1][2] - tr[0][1][2])
    # pose change start->end over all rotation tracks
    diff = 0
    peak = 0
    for b in a['bones'].values():
        r = b['rot']
        if r and len(r) > 1:
            diff += sum(abs(x - y) for x, y in zip(r[0][1], r[-1][1]))
            peak = max(peak, max(sum(abs(x - y) for x, y in zip(r[0][1], q)) for _, q in r))
    wy = [v[1] for _, v in (a['bones'].get('Waist', {}).get('trans') or [])]
    print(i, d[:4].hex(), 'frames', a['frames'], 'bones', len(a['bones']), 'travel %.0f' % travel, 'enddiff %.2f' % diff, 'peak %.2f' % peak,
          'waistY %.0f..%.0f' % (min(wy), max(wy)) if wy else '')
