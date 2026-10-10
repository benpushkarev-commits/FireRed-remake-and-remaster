"""Let's Go field .gfbmdl -> self-contained .glb (static scenery).

usage: gf2glb.py model.gfbmdl texdir out.glb [--sky] [--flipv]
Bones become nodes (Euler XYZ), meshes sit on their group's bone node.
Multi-texture field shaders are approximated with one base texture per
triangle (ground: vertex-colour split grass/soil; cliff: normal split).
"""
import sys, os, json, struct, math, io
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfbmdl

FS = {0: ('f', 4), 1: ('e', 2), 3: ('B', 1), 5: ('h', 2), 8: ('B', 1)}
BASE_SAMPLERS = ['Texture01', 'TextureMap01', 'Rock_tex', 'GrassTex01', 'GroundTex01', 'CliffTex01']
SKIP_SHADERS = {'FieldShadowOnlyShader'}

def quat_xyz(rx, ry, rz):
    cx, sx = math.cos(rx / 2), math.sin(rx / 2)
    cy, sy = math.cos(ry / 2), math.sin(ry / 2)
    cz, sz = math.cos(rz / 2), math.sin(rz / 2)
    # q = qz * qy * qx  (apply X, then Y, then Z)
    w = cz * cy * cx + sz * sy * sx
    x = cz * cy * sx - sz * sy * cx
    y = cz * sy * cx + sz * cy * sx
    z = sz * cy * cx - cz * sy * sx
    return [x, y, z, w]

def decode_vertices(mesh):
    attrs = mesh['attrs']
    stride = sum(FS[a['fmt']][1] * a['count'] for a in attrs)
    data = mesh['data']
    n = len(data) // stride
    cols = {}
    off = 0
    for a in attrs:
        ch, sz = FS[a['fmt']]
        fmt = '<' + ch * a['count']
        vals = [struct.unpack_from(fmt, data, v * stride + off) for v in range(n)]
        cols.setdefault(a['type'], vals)
        off += sz * a['count']
    return n, cols

def tex_name(m, mat, sampler):
    for s, i in mat['maps']:
        if s == sampler and 0 <= i < len(m['textures']):
            return m['textures'][i]
    return None

class GLB:
    def __init__(self):
        self.bin = bytearray()
        self.j = dict(asset=dict(version='2.0', generator='FireRedRemaster gf2glb'), scene=0, scenes=[dict(nodes=[])],
                      nodes=[], meshes=[], materials=[], textures=[], images=[], samplers=[dict(wrapS=10497, wrapT=10497)],
                      accessors=[], bufferViews=[], buffers=[])
        self.img_index, self.mat_index = {}, {}
    def view(self, b, target=None):
        while len(self.bin) % 4: self.bin.append(0)
        v = dict(buffer=0, byteOffset=len(self.bin), byteLength=len(b))
        if target: v['target'] = target
        self.bin += b
        self.j['bufferViews'].append(v)
        return len(self.j['bufferViews']) - 1
    def acc(self, vals, kind, comp=5126, target=34962, minmax=False):
        width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[kind]
        ch = {5126: 'f', 5125: 'I'}[comp]
        flat = [x for v in vals for x in (v if width > 1 else (v,))]
        a = dict(bufferView=self.view(struct.pack('<%d%s' % (len(flat), ch), *flat), target),
                 componentType=comp, count=len(vals), type=kind)
        if minmax:
            rows = [v if width > 1 else (v,) for v in vals]
            a['min'] = [min(v[k] for v in rows) for k in range(width)]
            a['max'] = [max(v[k] for v in rows) for k in range(width)]
        self.j['accessors'].append(a)
        return len(self.j['accessors']) - 1
    def image(self, path):
        if path not in self.img_index:
            iv = self.view(open(path, 'rb').read())
            self.j['images'].append(dict(bufferView=iv, mimeType='image/png', name=os.path.basename(path)[:-4]))
            self.j['textures'].append(dict(source=len(self.j['images']) - 1, sampler=0))
            self.img_index[path] = len(self.j['textures']) - 1
        return self.img_index[path]
    def material(self, key, texpath, mask, double, color=None):
        if key not in self.mat_index:
            pbr = dict(metallicFactor=0.0, roughnessFactor=1.0)
            if color: pbr['baseColorFactor'] = list(color)
            if texpath and os.path.exists(texpath):
                pbr['baseColorTexture'] = dict(index=self.image(texpath))
            mat = dict(name=key, pbrMetallicRoughness=pbr, doubleSided=double)
            if mask:
                mat['alphaMode'] = 'MASK'; mat['alphaCutoff'] = 0.5
            self.j['materials'].append(mat)
            self.mat_index[key] = len(self.j['materials']) - 1
        return self.mat_index[key]
    def save(self, path):
        while len(self.bin) % 4: self.bin.append(0)
        self.j['buffers'] = [dict(byteLength=len(self.bin))]
        js = json.dumps(self.j, separators=(',', ':')).encode()
        js += b' ' * ((4 - len(js) % 4) % 4)
        out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin))
        out += struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(self.bin), 0x004E4942) + self.bin
        open(path, 'wb').write(out)

def tree_texture(m, mat, texdir):
    """Bake the ramp-lit tree shader into one texture: Texture01 leaf alpha/detail over the
    average of the Texture02 colour ramp (the shader samples that ramp by lighting)."""
    from PIL import Image, ImageChops
    t1, t2 = tex_name(m, mat, 'Texture01'), tex_name(m, mat, 'Texture02')
    name = '%s__%s__tree' % (t1, t2)
    path = os.path.join(texdir, name + '.png')
    if not os.path.exists(path):
        leaf = Image.open(os.path.join(texdir, t1 + '.png')).convert('RGBA')
        ramp = Image.open(os.path.join(texdir, t2 + '.png')).convert('RGB')
        # upper-middle of the ramp = lit side colour
        col = ramp.crop((0, ramp.height // 4, ramp.width, ramp.height * 3 // 4)).resize((1, 1)).getpixel((0, 0))
        base = Image.new('RGB', leaf.size, col)
        detail = leaf.convert('RGB').point(lambda v: min(255, v * 3))
        rgb = ImageChops.add(base, detail)
        rgb.putalpha(leaf.getchannel('A'))
        rgb.save(path)
    return name

def choose(shader, mat, m, tri_n, tri_c):
    """Return (sampler, uvset) for one triangle."""
    names = [s for s, _ in mat['maps']]
    if shader == 'FieldWaterShader01':
        return None, 3   # the net/caustic maps are overlays; flat Base_Color reads correctly
    if shader in ('FieldTreeShader04', 'FieldTreeShader05') and 'Texture02' in names:
        return 'TREE', 3
    if shader.startswith('FieldGroundShader'):
        # vertex colour red channel = soil weight (checked against the in-game look)
        return ('GroundTex01', 3) if tri_c > 0.5 else ('GrassTex01', 3)
    if shader.startswith('FieldCliffShader') or shader.startswith('FieldRockShader'):
        side = 'CliffTex01' if 'CliffTex01' in names else 'Rock_tex'
        return ('GroundTex01', 3) if tri_n > 0.65 else (side, 4 if mat['values'].get('Tex01_UV', 0) == 1 else 3)
    for s in BASE_SAMPLERS:
        if s in names and tex_name(m, mat, s):
            return s, 3
    return None, 3

BUILDING_MATS = ('house', 'rooftile', 'window', 'doorway', 'tile01', 'lab')

def convert(mdl_path, texdir, out, sky=False, flipv=False, soil_channel=0, moves=(), root=None, clip=None):
    """moves: [(xmin, xmax, zmin, zmax, dx, dz)] in model units, applied to building-material
    vertices inside the rectangle (aligns each building to its FireRed footprint).
    root: (tx, tz, scale) wraps the scene so model units land on FireRed tile coordinates."""
    m = gfbmdl.load(open(mdl_path, 'rb').read())
    g = GLB()
    bones = m['bones']
    for b in bones:
        node = dict(name=b['name'])
        if any(b['trans']): node['translation'] = list(b['trans'])
        if any(b['rot']): node['rotation'] = quat_xyz(*b['rot'])
        if tuple(b['scale']) != (1, 1, 1): node['scale'] = list(b['scale'])
        g.j['nodes'].append(node)
    for i, b in enumerate(bones):
        if b['parent'] < 0 or b['parent'] >= len(bones) or b['parent'] == i:
            g.j['scenes'][0]['nodes'].append(i)
        else:
            g.j['nodes'][b['parent']].setdefault('children', []).append(i)
    skipped = set()
    for grp in m['groups']:
        bone = bones[grp['bone']] if grp['bone'] < len(bones) else None
        if bone and not sky and bone['name'].startswith('sky'):
            continue
        mesh = m['meshes'][grp['mesh']]
        n, cols = decode_vertices(mesh)
        pos = [tuple(v[:3]) for v in cols[0]]
        if clip:
            xs = [p[0] for p in pos]; zs = [p[2] for p in pos]
            if max(xs) < clip[0] or min(xs) > clip[1] or max(zs) < clip[2] or min(zs) > clip[3]:
                continue
        mat_names = {m['materials'][p['material']]['name'] for p in mesh['polys']}
        if moves and mat_names and all(any(k in n for k in BUILDING_MATS) for n in mat_names):
            moved = []
            for x, y, z in pos:
                for x0, x1, z0, z1, dx, dz in moves:
                    if x0 <= x <= x1 and z0 <= z <= z1:
                        x, z = x + dx, z + dz
                        break
                moved.append((x, y, z))
            pos = moved
        nrm = []
        for v in cols.get(1, [(0, 1, 0)] * n):
            l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1
            nrm.append((v[0] / l, v[1] / l, v[2] / l))
        colr = [c[soil_channel] / 255.0 for c in cols.get(7, [(0, 0, 0, 0)] * n)]
        pa = g.acc(pos, 'VEC3', minmax=True)
        na = g.acc(nrm, 'VEC3')
        uv_acc = {}
        def uva(t):
            if t not in uv_acc:
                src = cols.get(t) or cols.get(3) or [(0, 0)] * n
                uv_acc[t] = g.acc([(u[0], 1 - u[1] if flipv else u[1]) for u in src], 'VEC2')
            return uv_acc[t]
        prims = {}
        for poly in mesh['polys']:
            mat = m['materials'][poly['material']]
            shader = m['shaders'][0] if False else (mat['shader'] or '')
            if shader in SKIP_SHADERS:
                skipped.add(mat['name']); continue
            f = poly['faces']
            for t in range(0, len(f) - 2, 3):
                a, b2, c = f[t], f[t + 1], f[t + 2]
                tn = (nrm[a][1] + nrm[b2][1] + nrm[c][1]) / 3
                tc = (colr[a] + colr[b2] + colr[c]) / 3
                s, uvt = choose(shader, mat, m, tn, tc)
                prims.setdefault((poly['material'], s, uvt), []).extend((a, b2, c))
        primitives = []
        for (mi, s, uvt), idx in prims.items():
            mat = m['materials'][mi]
            tn = tree_texture(m, mat, texdir) if s == 'TREE' else (tex_name(m, mat, s) if s else None)
            mask = mat['switches'].get('DiscardEnable', False) or mat['switches'].get('TextureAlphaTestEnable', False)
            double = mask or mat['values'].get('CullMode', 0) == 2
            key = '%s|%s' % (mat['name'], tn)
            color = None
            if 'Base_Color2' in mat['colors']:
                # lift the deep base colour towards the bright in-game surface tone
                color = [min(1.0, 0.35 + 0.6 * v) for v in mat['colors']['Base_Color2']] + [1.0]
            mid = g.material(key, os.path.join(texdir, tn + '.png') if tn else None, mask, double, color)
            ia = g.acc(idx, 'SCALAR', comp=5125, target=34963)
            primitives.append(dict(attributes=dict(POSITION=pa, NORMAL=na, TEXCOORD_0=uva(uvt)), indices=ia, material=mid))
        if not primitives:
            continue
        g.j['meshes'].append(dict(name=bone['name'] if bone else 'mesh%d' % grp['mesh'], primitives=primitives))
        mesh_i = len(g.j['meshes']) - 1
        if bone is not None and 'mesh' not in g.j['nodes'][grp['bone']]:
            g.j['nodes'][grp['bone']]['mesh'] = mesh_i
        else:
            g.j['nodes'].append(dict(name='mesh%d' % mesh_i, mesh=mesh_i))
            g.j['scenes'][0]['nodes'].append(len(g.j['nodes']) - 1)
    if root:
        tx, tz, sc = root
        g.j['nodes'].append(dict(name='letsgo_root', translation=[tx, 0, tz], scale=[sc, sc, sc],
                                 children=g.j['scenes'][0]['nodes']))
        g.j['scenes'][0]['nodes'] = [len(g.j['nodes']) - 1]
    for k in ('textures', 'images'):
        if not g.j[k]: del g.j[k]
    g.save(out)
    return dict(meshes=len(g.j['meshes']), materials=len(g.j['materials']), images=len(g.j.get('images', [])), skipped=sorted(skipped))

if __name__ == '__main__':
    a = sys.argv[1:]
    print(convert(a[0], a[1], a[2], sky='--sky' in a, flipv='--flipv' in a))
