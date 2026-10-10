import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfbmdl, gf2glb
m = gfbmdl.load(open(sys.argv[1], 'rb').read())
for gi, grp in enumerate(m['groups']):
    mesh = m['meshes'][grp['mesh']]
    n, cols = gf2glb.decode_vertices(mesh)
    mats = sorted({m['materials'][p['material']]['name'] for p in mesh['polys']})
    xs = [p[0] for p in cols[0]]; ys = [p[1] for p in cols[0]]; zs = [p[2] for p in cols[0]]
    c = cols.get(7, [])
    ch = [ (min(v[k] for v in c), max(v[k] for v in c), sum(v[k] for v in c)//max(1,len(c))) for k in range(4)] if c else None
    print(m['bones'][grp['bone']]['name'], n, mats, 'X %.0f..%.0f Y %.0f..%.0f Z %.0f..%.0f' % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)), 'col', ch)
