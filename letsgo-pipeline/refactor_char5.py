p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\char2glb.py'
t = open(p, encoding='utf8').read()
old = """        ys = sorted(v[1] for grp in m['groups'] for v in decode_vertices(m['meshes'][grp['mesh']])[1][0])"""
new = """        # size from the main body mesh only (Bulbasaur's vines stand upright in the bind pose)
        body = max((decode_vertices(m['meshes'][grp['mesh']])[1][0] for grp in m['groups']), key=len)
        ys = sorted(v[1] for v in body)"""
assert old in t
t = t.replace(old, new, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
