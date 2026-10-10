p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\char2glb.py'
t = open(p, encoding='utf8').read()
pairs = [
('''def convert(pack, out, scale=0.01, texdir=None):''', '''def travel_speed(a):
    tr = (a['bones'].get('Origin') or {}).get('trans')
    if not tr or len(tr) < 2: return 0.0
    (x0, _, z0), (x1, _, z1) = tr[0][1], tr[-1][1]
    return math.hypot(x1 - x0, z1 - z0) / max(1, a['frames'] - 1) * a['fps']

def convert(pack, out, scale=0.01, texdir=None, clips=None, target_height=None):
    """clips: optional {glTF clip name: parsed gfbanm}; default finds trainer Idle/Walk/Run.
    target_height: optional height in tiles; overrides scale (used for battle previews)."""'''),
('''    clips = find_clips(anims)''', '''    if clips is None:
        clips = find_clips(anims)
    else:
        clips = {k: (a, travel_speed(a)) for k, a in clips.items() if a is not None}'''),
('''    g.j['nodes'].append(dict(name='letsgo_char', scale=[scale] * 3, children=roots + mesh_nodes))''',
 '''    if target_height:
        ys = [v[1] for grp in m['groups'] for v in decode_vertices(m['meshes'][grp['mesh']])[1][0]]
        scale = target_height / max(1e-3, max(ys) - min(ys))
    g.j['nodes'].append(dict(name='letsgo_char', scale=[scale] * 3, children=roots + mesh_nodes))'''),
('''    speeds = {k: round(v[1] * scale, 3) for k, v in clips.items()}''', '''    speeds = {k: round(v[1] * scale, 3) for k, v in clips.items()}
    info_scale = scale'''),
('''    return dict(bones=len(bones), meshes=len(g.j['meshes']), clips=list(clips), native_speed=speeds)''',
 '''    return dict(bones=len(bones), meshes=len(g.j['meshes']), clips=list(clips), native_speed=speeds, scale=round(info_scale, 5))'''),
]
for a, b in pairs:
    assert a in t, a[:60]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
