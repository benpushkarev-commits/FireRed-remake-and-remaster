p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\char2glb.py'
t = open(p, encoding='utf8').read()
old = """            prims.append(dict(attributes=attrs, indices=g.acc(poly['faces'], 'SCALAR', comp=5125, target=34963), material=mid))"""
new = """            pa = attrs
            su, sv = mat['values'].get('ColorUVScaleU', 1.0), mat['values'].get('ColorUVScaleV', 1.0)
            tu, tv = mat['values'].get('ColorUVTranslateU', 0.0), mat['values'].get('ColorUVTranslateV', 0.0)
            if (su, sv, tu, tv) != (1.0, 1.0, 0.0, 0.0):
                # Pokemon eyes select an atlas cell with a per-material UV transform (uv * scale + translate)
                raw = cols.get(3, [(0, 0)] * n)
                tuv = [(u[0] * su + tu, (u[1] * sv + tv)) for u in raw]
                tuv = [(u, 1.0 - v if flip_v else v) for u, v in tuv]
                pa = dict(attrs, TEXCOORD_0=g.acc(tuv, 'VEC2'))
            prims.append(dict(attributes=pa, indices=g.acc(poly['faces'], 'SCALAR', comp=5125, target=34963), material=mid))"""
assert old in t
t = t.replace(old, new, 1)
open(p, 'w', encoding='utf8').write(t)
p2 = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\import_letsgo_pokemon.py'
t = open(p2, encoding='utf8').read().replace(', flip_v=False)', ')')
open(p2, 'w', encoding='utf8').write(t)
print('ok')
