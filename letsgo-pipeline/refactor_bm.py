p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\tools\letsgo\gf2glb.py'
t = open(p, encoding='utf8').read()
pairs = [
("BASE_SAMPLERS = ['Texture01', 'TextureMap01', 'Rock_tex', 'GrassTex01', 'GroundTex01', 'CliffTex01']",
 "BASE_SAMPLERS = ['Texture01', 'TextureMap01', 'Rock_tex', 'GrassTex01', 'GroundTex01', 'CliffTex01', 'Ground00Tex01']"),
("""            mask = mat['switches'].get('DiscardEnable', False) or mat['switches'].get('TextureAlphaTestEnable', False)""",
 """            # BlendMode 5 = alpha cut-outs (battle backdrop cards, foliage); render as MASK
            mask = mat['switches'].get('DiscardEnable', False) or mat['switches'].get('TextureAlphaTestEnable', False) \\
                or mat['values'].get('BlendMode', 0) == 5"""),
]
for a, b in pairs:
    assert a in t, a[:60]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
