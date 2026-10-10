"""Convert one field map gfpak (e.g. area02/town001_00.gfpak) to GLB using its area texture pack."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, bntx, gf2glb

ROOT = r'C:\Users\benpu\Desktop\LetsGo_Assets\bin\archive\field'
WORK = os.path.dirname(os.path.abspath(__file__))

def run(area, name, out, flipv=False, sky=False):
    texdir = os.path.join(WORK, 'tex', area)
    if not os.path.isdir(texdir):
        for _, d in gfpak.read(os.path.join(ROOT, area + '.gfpak')):
            if d[:4] == b'BNTX': bntx.export(d, texdir)
    files = gfpak.read(os.path.join(ROOT, area, name + '.gfpak'))
    mdl = max(files, key=lambda f: len(f[1]))[1]  # biggest entry is the gfbmdl
    tmp = os.path.join(WORK, 'tmp_' + name + '.gfbmdl')
    open(tmp, 'wb').write(mdl)
    # map-local textures (rare) also land in the area folder
    for _, d in files:
        if d[:4] == b'BNTX': bntx.export(d, texdir)
    return gf2glb.convert(tmp, texdir, out, sky=sky, flipv=flipv)

if __name__ == '__main__':
    a = sys.argv[1:]
    print(run(a[0], a[1], a[2], flipv='--flipv' in a, sky='--sky' in a))
