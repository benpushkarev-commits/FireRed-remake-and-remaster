"""Let's Go trainer pack (bin/archive/chara/data/tr/*.gfpak) -> skinned .glb with real clips.

usage: char2glb.py <pack.gfpak> <out.glb> [--scale 0.01]
Clips Idle / Walk / Run are found by motion: Walk/Run are the field loops whose
Origin travels forward (Run the faster), Idle is the still loop stored just before
Walk. Forward root travel is removed so the slice moves the character itself.
"""
import sys, os, math, struct, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, gfbmdl, gfbanm, bntx
from gf2glb import GLB, quat_xyz, decode_vertices, tex_name

def trs_matrix(t, q, s):
    x, y, z, w = q
    r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = r * np.array(s)[None, :]
    m[:3, 3] = t
    return m

def find_clips(anims):
    """anims: [(file index, parsed)] -> dict Idle/Walk/Run -> parsed."""
    def travel(a):
        tr = (a['bones'].get('Origin') or {}).get('trans')
        if not tr or len(tr) < 2: return 0.0
        (x0, _, z0), (x1, _, z1) = tr[0][1], tr[-1][1]
        return math.hypot(x1 - x0, z1 - z0) / max(1, a['frames'] - 1) * a['fps']
    moving = [(travel(a), i, a) for i, a in anims if travel(a) > 1.0]
    moving.sort(key=lambda v: v[0])
    if len(moving) < 2:
        raise ValueError('no walk/run loops found')
    walk, run = moving[0], moving[-1]
    before = [(i, a) for i, a in anims if i < walk[1] and travel(a) < 1.0 and a['frames'] >= 40]
    idle = max(before, key=lambda v: v[0])[1] if before else anims[0][1]
    return dict(Idle=(idle, 0.0), Walk=(walk[2], walk[0]), Run=(run[2], run[0]))

def convert(pack, out, scale=0.01, texdir=None):
    files = gfpak.read(pack)
    texdir = texdir or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work', 'tex', os.path.basename(pack)[:-6])
    if not os.path.isdir(texdir):
        for _, d in files:
            if d[:4] == b'BNTX': bntx.export(d, texdir)
    mdl = max((d for _, d in files if d[:4] == b'\x20\x00\x00\x00'), key=len)
    m = gfbmdl.load(mdl)
    anims = []
    for i, (_, d) in enumerate(files):
        if d[:4] == b'\x04\x00\x00\x00' and gfbanm.is_gfbanm(d):
            anims.append((i, gfbanm.load(d)))
    clips = find_clips(anims)
    g = GLB()
    bones = m['bones']
    bind = []
    for b in bones:
        q = quat_xyz(*b['rot'])
        node = dict(name=b['name'], translation=list(b['trans']), rotation=q, scale=list(b['scale']))
        g.j['nodes'].append(node)
        bind.append((tuple(b['trans']), tuple(q), tuple(b['scale'])))
    roots = []
    for i, b in enumerate(bones):
        p = b['parent']
        if 0 <= p < len(bones) and p != i:
            g.j['nodes'][p].setdefault('children', []).append(i)
        else:
            roots.append(i)
    world = [None] * len(bones)
    def w(i):
        if world[i] is None:
            loc = trs_matrix(*bind[i])
            p = bones[i]['parent']
            world[i] = (w(p) @ loc) if 0 <= p < len(bones) and p != i else loc
        return world[i]
    ibm = [np.linalg.inv(w(i)) for i in range(len(bones))]
    flat = [v for mtx in ibm for v in mtx.T.flatten()]   # column-major
    ibm_view = g.view(struct.pack('<%df' % len(flat), *flat))
    g.j['accessors'].append(dict(bufferView=ibm_view, componentType=5126, count=len(bones), type='MAT4'))
    g.j['skins'] = [dict(joints=list(range(len(bones))), inverseBindMatrices=len(g.j['accessors']) - 1, skeleton=roots[0])]
    skin_list = [i for i, b in enumerate(bones) if not b['rigid']]
    mesh_nodes = []
    for grp in m['groups']:
        mesh = m['meshes'][grp['mesh']]
        n, cols = decode_vertices(mesh)
        pos = [tuple(v[:3]) for v in cols[0]]
        nrm = []
        for v in cols.get(1, [(0, 1, 0)] * n):
            l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1
            nrm.append((v[0] / l, v[1] / l, v[2] / l))
        uv = [(u[0], 1.0 - u[1]) for u in cols.get(3, [(0, 0)] * n)]   # GF V runs bottom-up
        attrs = dict(POSITION=g.acc(pos, 'VEC3', minmax=True), NORMAL=g.acc(nrm, 'VEC3'), TEXCOORD_0=g.acc(uv, 'VEC2'))
        if 11 in cols and 12 in cols:
            joints, weights = [], []
            for ids, ws in zip(cols[11], cols[12]):
                ws = [x / 255.0 for x in ws]
                tot = sum(ws) or 1.0
                joints.append(tuple(skin_list[k] if k < len(skin_list) else 0 for k in ids))
                weights.append(tuple(x / tot for x in ws))
            jb = struct.pack('<%dH' % (4 * n), *[x for j in joints for x in j])
            jv = g.view(jb, 34962)
            g.j['accessors'].append(dict(bufferView=jv, componentType=5123, count=n, type='VEC4'))
            attrs['JOINTS_0'] = len(g.j['accessors']) - 1
            wb = struct.pack('<%df' % (4 * n), *[x for wt in weights for x in wt])
            wv = g.view(wb, 34962)
            g.j['accessors'].append(dict(bufferView=wv, componentType=5126, count=n, type='VEC4'))
            attrs['WEIGHTS_0'] = len(g.j['accessors']) - 1
        prims = []
        for poly in mesh['polys']:
            mat = m['materials'][poly['material']]
            tn = tex_name(m, mat, 'L0ColTex')
            mid = g.material(mat['name'], os.path.join(texdir, tn + '.png') if tn else None, False, False)
            prims.append(dict(attributes=attrs, indices=g.acc(poly['faces'], 'SCALAR', comp=5125, target=34963), material=mid))
        g.j['meshes'].append(dict(name=bones[grp['bone']]['name'], primitives=prims))
        node = dict(name='mesh_' + bones[grp['bone']]['name'], mesh=len(g.j['meshes']) - 1)
        if 'JOINTS_0' in attrs: node['skin'] = 0
        g.j['nodes'].append(node)
        mesh_nodes.append(len(g.j['nodes']) - 1)
    g.j['nodes'].append(dict(name='letsgo_char', scale=[scale] * 3, children=roots + mesh_nodes))
    g.j['scenes'][0]['nodes'] = [len(g.j['nodes']) - 1]
    index = {b['name']: i for i, b in enumerate(bones)}
    g.j['animations'] = []
    for name, (a, _) in clips.items():
        chans, samps = [], []
        fps = float(a['fps'])
        for bname, tr in a['bones'].items():
            if bname not in index: continue
            ni = index[bname]
            for path, keys in (('translation', tr['trans']), ('rotation', tr['rot']), ('scale', tr['scale'])):
                if not keys: continue
                if path == 'translation' and bname == 'Origin':
                    keys = [(f, (0.0, v[1], 0.0)) for f, v in keys]   # in-place locomotion
                times = [f / fps for f, _ in keys]
                ia = g.acc(times, 'SCALAR', comp=5126, target=None, minmax=True)
                kind = 'VEC4' if path == 'rotation' else 'VEC3'
                vals = [tuple(v) for _, v in keys]
                if kind == 'VEC4':
                    flatv = [x for v in vals for x in v]
                    vv = g.view(struct.pack('<%df' % len(flatv), *flatv))
                    g.j['accessors'].append(dict(bufferView=vv, componentType=5126, count=len(vals), type='VEC4'))
                    oa = len(g.j['accessors']) - 1
                else:
                    oa = g.acc(vals, 'VEC3', target=None)
                samps.append(dict(input=ia, output=oa, interpolation='LINEAR'))
                chans.append(dict(sampler=len(samps) - 1, target=dict(node=ni, path=path)))
        g.j['animations'].append(dict(name=name, channels=chans, samplers=samps))
    for k in ('textures', 'images'):
        if not g.j[k]: del g.j[k]
    g.save(out)
    speeds = {k: round(v[1] * scale, 3) for k, v in clips.items()}
    return dict(bones=len(bones), meshes=len(g.j['meshes']), clips=list(clips), native_speed=speeds)

if __name__ == '__main__':
    a = sys.argv[1:]
    sc = float(a[a.index('--scale') + 1]) if '--scale' in a else 0.01
    print(json.dumps(convert(a[0], a[1], sc)))
