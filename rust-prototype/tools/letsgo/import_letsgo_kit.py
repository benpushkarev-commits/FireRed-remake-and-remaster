"""Let's Go "kit" pieces for FireRed maps that have no matching Let's Go layout.

usage: python import_letsgo_kit.py <Let's Go RomFS root> <out assets dir>
Writes <out>/kit/<name>.glb (centred on the tile, base at y=0, 0.01 scale) and
<out>/kit/kit.json. Pieces: one canopy tree split out of Pallet's tree cluster,
encounter grass, flowers, the cut-able tree, signpost and the ground textures.
"""
import os, sys, json, math, shutil, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, bntx, gfbmdl, gf2glb
from gf2glb import GLB, decode_vertices, tex_name, choose, tree_texture, lg_extras

S = 0.01

def piece_from_tris(m, texdir, tris, out, center):
    """tris: [(material index, (a,b,c), mesh cols)] -> recentred GLB."""
    g = GLB()
    cx, y0, cz = center
    prims = {}
    for mi, (a, b, c), cols, nrm in tris:
        prims.setdefault(mi, []).append((a, b, c, cols, nrm))
    # soft canopy normals (see gf2glb.canopy_normals), per source mesh
    bent = {}
    for mi, (a, b2, c), cols, nrm in tris:
        if (m['materials'][mi]['shader'] or '').startswith('FieldTreeShader'):
            bent.setdefault(id(nrm), (cols, nrm, set()))[2].update((a, b2, c))
    fixed = {k: gf2glb.canopy_normals([tuple(p[:3]) for p in cols[0]], nrm, list(vs)) for k, (cols, nrm, vs) in bent.items()}
    prims = {mi: [(a, b2, c, cols, fixed.get(id(nrm), nrm)) for a, b2, c, cols, nrm in faces] for mi, faces in prims.items()}
    primitives = []
    for mi, faces in prims.items():
        mat = m['materials'][mi]
        pos, nr, uv, uv1, vc, idx = [], [], [], [], [], []
        for a, b, c, cols, nrm in faces:
            for v in (a, b, c):
                p = cols[0][v]
                pos.append(((p[0] - cx) * S, (p[1] - y0) * S, (p[2] - cz) * S))
                nr.append(nrm[v])
                u = cols.get(3, [(0, 0)] * (v + 1))[v]
                uv.append((u[0], u[1]))
                w = cols.get(4, [(0, 0)] * (v + 1))[v]
                uv1.append((w[0], w[1]))
                k = cols.get(7, [(255, 255, 255, 255)] * (v + 1))[v]
                vc.append(tuple(x / 255.0 for x in k))
                idx.append(len(idx))
        s, _ = choose(mat['shader'] or '', mat, m, 1.0, 0.0)
        tn = tree_texture(m, mat, texdir) if s == 'TREE' else (tex_name(m, mat, s) if s else None)
        mask = mat['switches'].get('DiscardEnable', False) or mat['switches'].get('TextureAlphaTestEnable', False)
        ex = lg_extras(g, m, mat, mat['shader'] or '', texdir, tn)
        cutoff = min(ex['discard'], 0.6) if mask and ex['discard'] > 0 else 0.5
        mid = g.material(mat['name'], os.path.join(texdir, tn + '.png') if tn else None, mask, True, None, ex, cutoff)
        primitives.append(dict(attributes=dict(POSITION=g.acc(pos, 'VEC3', minmax=True), NORMAL=g.acc(nr, 'VEC3'),
                                               TEXCOORD_0=g.acc(uv, 'VEC2'), TEXCOORD_1=g.acc(uv1, 'VEC2'), COLOR_0=g.acc(vc, 'VEC4')),
                               indices=g.acc(idx, 'SCALAR', comp=5125, target=34963), material=mid))
    g.j['meshes'].append(dict(name=os.path.basename(out)[:-4], primitives=primitives))
    g.j['nodes'].append(dict(name=os.path.basename(out)[:-4], mesh=0))
    g.j['scenes'][0]['nodes'] = [0]
    for k in ('textures', 'images'):
        if not g.j[k]: del g.j[k]
    g.save(out)

def norms(cols, n):
    out = []
    for v in cols.get(1, [(0, 1, 0)] * n):
        l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1
        out.append((v[0] / l, v[1] / l, v[2] / l))
    return out

def whole_model(d, texdir, out):
    m = gfbmdl.load(d)
    tris, ys, xs, zs = [], [], [], []
    for grp in m['groups']:
        mesh = m['meshes'][grp['mesh']]
        n, cols = decode_vertices(mesh)
        nr = norms(cols, n)
        for poly in mesh['polys']:
            if (m['materials'][poly['material']]['shader'] or '') in gf2glb.SKIP_SHADERS:
                continue
            f = poly['faces']
            for t in range(0, len(f) - 2, 3):
                tris.append((poly['material'], (f[t], f[t + 1], f[t + 2]), cols, nr))
        xs += [p[0] for p in cols[0]]; ys += [p[1] for p in cols[0]]; zs += [p[2] for p in cols[0]]
    piece_from_tris(m, texdir, tris, out, ((min(xs) + max(xs)) / 2, min(ys), (min(zs) + max(zs)) / 2))
    return dict(size=[round((max(xs) - min(xs)) * S, 2), round((max(ys) - min(ys)) * S, 2), round((max(zs) - min(zs)) * S, 2)])

def one_tree(mdl, texdir, out, materials, near):
    """Split the clustered tree mesh into single trees by connectivity + proximity, keep the one nearest `near`."""
    m = gfbmdl.load(mdl)
    tris = []
    for grp in m['groups']:
        mesh = m['meshes'][grp['mesh']]
        names = {m['materials'][p['material']]['name'] for p in mesh['polys']}
        if not names & set(materials):
            continue
        n, cols = decode_vertices(mesh)
        nr = norms(cols, n)
        for poly in mesh['polys']:
            if m['materials'][poly['material']]['name'] not in materials:
                continue
            f = poly['faces']
            for t in range(0, len(f) - 2, 3):
                tris.append((poly['material'], (f[t], f[t + 1], f[t + 2]), cols, nr))
    def xz(tri):
        cols = tri[2]
        a, b, c = tri[1]
        return ((cols[0][a][0] + cols[0][b][0] + cols[0][c][0]) / 3, (cols[0][a][2] + cols[0][b][2] + cols[0][c][2]) / 3)
    # trees are ~2-3 m apart: take every triangle within 130 units (XZ) of the trunk nearest `near`
    canopy = [t for t in tris if 'miki' not in m['materials'][t[0]]['name']]
    seed = min(canopy, key=lambda t: math.dist(xz(t), near))
    near_c = [xz(t) for t in canopy if math.dist(xz(t), xz(seed)) < 150]
    bx = sum(p[0] for p in near_c) / len(near_c)
    bz = sum(p[1] for p in near_c) / len(near_c)
    pick = [t for t in tris if math.dist(xz(t), (bx, bz)) < 150]
    ys = [t[2][0][v][1] for t in pick for v in t[1]]
    piece_from_tris(m, texdir, pick, out, (bx, min(ys), bz))
    return dict(triangles=len(pick), height=round((max(ys) - min(ys)) * S, 2))

def main(romfs, out):
    field = os.path.join(romfs, 'bin', 'archive', 'field')
    work = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work')
    kit = os.path.join(out, 'kit')
    os.makedirs(kit, exist_ok=True)
    manifest = {}
    # build pieces
    texdir = os.path.join(work, 'tex', 'buildmodel')
    files = gfpak.read(os.path.join(field, 'buildmodel.gfpak'))
    if not os.path.isdir(texdir):
        for _, d in files:
            if d[:4] == b'BNTX': bntx.export(d, texdir)
    # buildmodel pieces reuse area textures too
    area = os.path.join(work, 'tex', 'area02')
    if os.path.isdir(area):
        for f in os.listdir(area):
            if not os.path.exists(os.path.join(texdir, f)):
                shutil.copy2(os.path.join(area, f), texdir)
    wanted = {'enc_grass01': 'grass', 'flower01': 'flower', 'flowers03': 'flowers', 'iaigiri_tree01': 'cut_tree', 'kairiki_rock': 'boulder'}
    for _, d in files:
        if d[:4] != b'\x20\x00\x00\x00':
            continue
        m = gfbmdl.load(d)
        name = m['bones'][0]['name'] if m['bones'] else ''
        if name in wanted:
            dst = os.path.join(kit, wanted[name] + '.glb')
            manifest[wanted[name]] = dict(path='letsgo/kit/%s.glb' % wanted[name], source='buildmodel/' + name, **whole_model(d, texdir, dst))
    # tree from Pallet's cluster
    tfiles = gfpak.read(os.path.join(field, 'area02', 'town001_00.gfpak'))
    mdl = max(tfiles, key=lambda f: len(f[1]))[1]
    manifest['tree'] = dict(path='letsgo/kit/tree.glb', source='area02/town001_00 tree cluster',
                            **one_tree(mdl, area, os.path.join(kit, 'tree.glb'), {'tree001_newsha1', 'area02_tree2_tree_miki'}, (0, 1500)))
    # ground textures for tiles
    for name in ('grass01_com', 'soil01_com', 'cliff01_com', 'rock01_com'):
        shutil.copy2(os.path.join(area, name + '.png'), os.path.join(kit, name + '.png'))
        manifest[name] = dict(path='letsgo/kit/%s.png' % name)
    json.dump(manifest, open(os.path.join(kit, 'kit.json'), 'w'), indent=1)
    print(json.dumps(manifest, indent=1))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
