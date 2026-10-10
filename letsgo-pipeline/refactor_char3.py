p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\char2glb.py'
t = open(p, encoding='utf8').read()
pairs = [
("""            if tn and layer and 'dummy' not in layer:
                tn = layered(texdir, tn, layer)""", """            # the iris layer has its own UV transform; the colour map already has painted eyes"""),
("""    mdl = max((d for _, d in files if d[:4] == b'\\x20\\x00\\x00\\x00'), key=len)
    m = gfbmdl.load(mdl)""", """    # Pokemon packs hold the normal and the shiny (_rare) model: take the normal one.
    cands = [gfbmdl.load(d) for _, d in files if d[:4] == b'\\x20\\x00\\x00\\x00']
    m = min(cands, key=lambda c: (sum('_rare' in n for n in c['textures']), -len(c['meshes'])))"""),
]
for a, b in pairs:
    assert a in t, a[:60]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
