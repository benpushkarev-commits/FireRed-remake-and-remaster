"""Import Let's Go field scenery into the Rust slice.

usage: python import_letsgo_field.py <Let's Go RomFS root> <out assets dir>
Reads bin/archive/field/<area>.gfpak (textures) and <area>/<model>.gfpak,
writes <out>/<map>_<n>.glb and <out>/scenery.json. Everything stays local.
The root transform maps Let's Go units (100 per tile) onto FireRed tile
coordinates; per-building moves snap each house onto its FireRed footprint.
"""
import os, sys, json, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, bntx, gf2glb

MAPS = {
    'pallet': dict(
        area='area02',
        root=(-8.86, 0.09, 0.01),
        models=[('town001_00', None), ('sea_00', (-2500, 6000, -500, 4500))],
        moves=[
            (1350, 1950, 250, 700, -60, 0),   # player house -> FireRed (5..9, 4..6)
            (2150, 2750, 250, 700, 44, 0),    # rival house  -> FireRed (14..18, 4..6)
        ]),
}

def main(romfs, out, work):
    field = os.path.join(romfs, 'bin', 'archive', 'field')
    os.makedirs(out, exist_ok=True)
    os.makedirs(work, exist_ok=True)
    manifest = {}
    for mid, cfg in MAPS.items():
        area = cfg['area']
        texdir = os.path.join(work, 'tex', area)
        if not os.path.isdir(texdir):
            for _, d in gfpak.read(os.path.join(field, area + '.gfpak')):
                if d[:4] == b'BNTX':
                    bntx.export(d, texdir)
        entries = []
        for i, (name, clip) in enumerate(cfg['models']):
            files = gfpak.read(os.path.join(field, area, name + '.gfpak'))
            mdl = max(files, key=lambda f: len(f[1]))[1]
            tmp = os.path.join(work, name + '.gfbmdl')
            open(tmp, 'wb').write(mdl)
            dst = os.path.join(out, '%s_%d.glb' % (mid, i))
            info = gf2glb.convert(tmp, texdir, dst, moves=cfg['moves'], root=cfg['root'], clip=clip)
            entries.append(dict(path='letsgo/%s_%d.glb' % (mid, i), source='bin/archive/field/%s/%s.gfpak' % (area, name),
                                sha256=hashlib.sha256(open(dst, 'rb').read()).hexdigest(), **info))
            print(mid, name, info)
        manifest[mid] = entries
    json.dump(manifest, open(os.path.join(out, 'scenery.json'), 'w'), indent=1)

if __name__ == '__main__':
    work = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work')
    main(sys.argv[1], sys.argv[2], work)
