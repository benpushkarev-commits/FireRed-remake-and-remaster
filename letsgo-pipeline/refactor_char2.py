p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\char2glb.py'
t = open(p, encoding='utf8').read()
pairs = [
('''            tn = tex_name(m, mat, 'L0ColTex')''', '''            tn = tex_name(m, mat, 'L0ColTex') or tex_name(m, mat, 'Col0Tex')
            layer = tex_name(m, mat, 'LyCol0Tex')
            if tn and layer and 'dummy' not in layer:
                tn = layered(texdir, tn, layer)'''),
('''def travel_speed(a):''', '''def layered(texdir, base, top):
    """Pokemon eyes: the iris layer (LyCol0Tex) sits over the sclera colour map."""
    from PIL import Image
    name = '%s__%s' % (base, top)
    path = os.path.join(texdir, name + '.png')
    if not os.path.exists(path):
        b = Image.open(os.path.join(texdir, base + '.png')).convert('RGBA')
        l = Image.open(os.path.join(texdir, top + '.png')).convert('RGBA').resize(b.size)
        Image.alpha_composite(b, l).save(path)
    return name

def travel_speed(a):'''),
('''        ys = [v[1] for grp in m['groups'] for v in decode_vertices(m['meshes'][grp['mesh']])[1][0]]
        scale = target_height / max(1e-3, max(ys) - min(ys))''', '''        ys = sorted(v[1] for grp in m['groups'] for v in decode_vertices(m['meshes'][grp['mesh']])[1][0])
        lo, hi = ys[int(len(ys) * 0.01)], ys[int(len(ys) * 0.99) - 1]   # ignore stray vine / effect verts
        scale = target_height / max(1e-3, hi - lo)'''),
]
for a, b in pairs:
    assert a in t, a[:60]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
