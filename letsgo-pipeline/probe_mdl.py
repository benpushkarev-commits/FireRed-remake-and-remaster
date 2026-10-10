import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfbmdl
m = gfbmdl.load(open(sys.argv[1], 'rb').read())
print('textures', len(m['textures']), m['textures'][:60])
print('materials', len(m['materials']), 'meshes', len(m['meshes']), 'bones', len(m['bones']), 'groups', len(m['groups']))
for mat in m['materials'][:40]:
    print(' M', mat['name'], mat['shader'], [(s, m['textures'][i] if 0 <= i < len(m['textures']) else i) for s, i in mat['maps']])
    print('    sw', {k: v for k, v in mat['switches'].items() if v and k in ('TextureAlphaTestEnable', 'DiscardEnable', 'Transparent', 'TextureBlend', 'GroundBlend')},
          {k: v for k, v in mat['values'].items() if 'Scale' in k or k in ('BlendMode', 'CullMode', 'Tex01_UV', 'Tex00_UV')})
c = collections.Counter()
for me in m['meshes']:
    c[tuple((a['type'], a['fmt'], a['count']) for a in me['attrs'])] += 1
print(c)
for b in m['bones'][:12]:
    print(' B', b)
print(m['groups'][:10])
