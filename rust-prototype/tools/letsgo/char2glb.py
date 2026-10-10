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

KIND_CHAR, KIND_FIRE = 8, 9

def char_extras(g, m, mat, texdir, base_tex):
    """Let's Go character / Pokemon shading for src/letsgo_material.rs (None = skip the primitive)."""
    shader = mat['shader'] or ''
    if shader.startswith('PokeFireMask'):
        return None   # stencil-only pass
    def tex(sampler):
        t = tex_name(m, mat, sampler)
        if not t or t.startswith('dummy'): return -1
        path = os.path.join(texdir, t + '.png')
        return g.image(path) if os.path.exists(path) else -1
    c = mat['colors']
    if shader.startswith('PokeFireCore'):
        return dict(kind=KIND_FIRE, shader=shader, tex=[tex('Blend0Tex'), tex('Blend1Tex'), tex('LerpTex'), -1, -1, -1], cloud=-1,
                    discard=0.0, blend5=True, side_uv=0, rim=[0, 0, 0], shadow=[1, 1, 1], rim_color=[0, 0, 0],
                    base_color=list(c.get('BaseColor0', (1, 0.1, 0))), deep_color=list(c.get('LayerColor0', (1, 0.6, 0))), scroll=[0] * 8)
    base = g.image(os.path.join(texdir, base_tex + '.png')) if base_tex and os.path.exists(os.path.join(texdir, base_tex + '.png')) else -1
    return dict(kind=KIND_CHAR, shader=shader, tex=[base, tex('ColSdw0Tex'), tex('AmbientTex'), tex('LightTblTex'), -1, -1], cloud=-1,
                discard=0.0, blend5=False, side_uv=0, rim=[0.55, 1.0, 0.35], shadow=list(c.get('ShadowColor', (0.5, 0.5, 0.5))),
                rim_color=list(c.get('RimColor', (1, 1, 1))), base_color=[1, 1, 1],
                deep_color=list(c.get('DeepShadowColor', (0.49, 0.52, 0.7))), scroll=[0] * 8)

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

def layered(texdir, base, top):
    """Pokemon eyes: the iris layer (LyCol0Tex) sits over the sclera colour map."""
    from PIL import Image
    name = '%s__%s' % (base, top)
    path = os.path.join(texdir, name + '.png')
    if not os.path.exists(path):
        b = Image.open(os.path.join(texdir, base + '.png')).convert('RGBA')
        l = Image.open(os.path.join(texdir, top + '.png')).convert('RGBA').resize(b.size)
        Image.alpha_composite(b, l).save(path)
    return name

def travel_speed(a):
    tr = (a['bones'].get('Origin') or {}).get('trans')
    if not tr or len(tr) < 2: return 0.0
    (x0, _, z0), (x1, _, z1) = tr[0][1], tr[-1][1]
    return math.hypot(x1 - x0, z1 - z0) / max(1, a['frames'] - 1) * a['fps']

def convert(pack, out, scale=0.01, texdir=None, clips=None, target_height=None, flip_v=True):
    """clips: optional {glTF clip name: parsed gfbanm}; default finds trainer Idle/Walk/Run.
    target_height: optional height in tiles; overrides scale (used for battle previews)."""
    files = gfpak.read(pack)
    texdir = texdir or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work', 'tex', os.path.basename(pack)[:-6])
    if not os.path.isdir(texdir):
        for _, d in files:
            if d[:4] == b'BNTX': bntx.export(d, texdir)
    # Pokemon packs hold the normal and the shiny (_rare) model: take the normal one.
    cands = [gfbmdl.load(d) for _, d in files if d[:4] == b'\x20\x00\x00\x00']
    m = min(cands, key=lambda c: (sum('_rare' in n for n in c['textures']), -len(c['meshes'])))
    anims = []
    for i, (_, d) in enumerate(files):
        if d[:4] == b'\x04\x00\x00\x00' and gfbanm.is_gfbanm(d):
            anims.append((i, gfbanm.load(d)))
    if clips is None:
        clips = find_clips(anims)
    else:
        clips = {k: (a, travel_speed(a)) for k, a in clips.items() if a is not None}
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
        uv = [(u[0], 1.0 - u[1] if flip_v else u[1]) for u in cols.get(3, [(0, 0)] * n)]   # trainer V runs bottom-up
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
            tn = tex_name(m, mat, 'L0ColTex') or tex_name(m, mat, 'Col0Tex')
            layer = tex_name(m, mat, 'LyCol0Tex')
            # the iris layer has its own UV transform; the colour map already has painted eyes
            ex = char_extras(g, m, mat, texdir, tn)
            if ex is None:
                continue
            mid = g.material(mat['name'], os.path.join(texdir, tn + '.png') if tn else None, False, False, None, ex)
            pa = attrs
            su, sv = mat['values'].get('ColorUVScaleU', 1.0), mat['values'].get('ColorUVScaleV', 1.0)
            tu, tv = mat['values'].get('ColorUVTranslateU', 0.0), mat['values'].get('ColorUVTranslateV', 0.0)
            if (su, sv, tu, tv) != (1.0, 1.0, 0.0, 0.0):
                # Pokemon eyes select an atlas cell with a per-material UV transform (uv * scale + translate)
                raw = cols.get(3, [(0, 0)] * n)
                tuv = [(u[0] * su + tu, (u[1] * sv + tv)) for u in raw]
                tuv = [(u, 1.0 - v if flip_v else v) for u, v in tuv]
                pa = dict(attrs, TEXCOORD_0=g.acc(tuv, 'VEC2'))
            prims.append(dict(attributes=pa, indices=g.acc(poly['faces'], 'SCALAR', comp=5125, target=34963), material=mid))
        g.j['meshes'].append(dict(name=bones[grp['bone']]['name'], primitives=prims))
        node = dict(name='mesh_' + bones[grp['bone']]['name'], mesh=len(g.j['meshes']) - 1)
        if 'JOINTS_0' in attrs: node['skin'] = 0
        g.j['nodes'].append(node)
        mesh_nodes.append(len(g.j['nodes']) - 1)
    if target_height:
        # size from the main body mesh only (Bulbasaur's vines stand upright in the bind pose)
        body = max((decode_vertices(m['meshes'][grp['mesh']])[1][0] for grp in m['groups']), key=len)
        ys = sorted(v[1] for v in body)
        lo, hi = ys[int(len(ys) * 0.01)], ys[int(len(ys) * 0.99) - 1]   # ignore stray vine / effect verts
        scale = target_height / max(1e-3, hi - lo)
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
    info_scale = scale
    return dict(bones=len(bones), meshes=len(g.j['meshes']), clips=list(clips), native_speed=speeds, scale=round(info_scale, 5))

if __name__ == '__main__':
    a = sys.argv[1:]
    sc = float(a[a.index('--scale') + 1]) if '--scale' in a else 0.01
    print(json.dumps(convert(a[0], a[1], sc)))
