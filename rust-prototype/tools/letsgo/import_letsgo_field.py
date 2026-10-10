"""Import Let's Go field scenery into the Rust slice.

usage: python import_letsgo_field.py <Let's Go RomFS root> <out assets dir>
Reads bin/archive/field/<area>.gfpak (textures) and <area>/<model>.gfpak,
writes <out>/<map>_<n>.glb, <out>/<map>_height.json and <out>/scenery.json.
The root transform maps Let's Go units (100 per tile) onto FireRed tile
coordinates. Area 02 shares one Let's Go coordinate frame, so Route 1 is
Pallet's root shifted by Route 1's FireRed connection offset (0, -40).

overlay=True: the FireRed layout differs (Route 1 has ledges and tree rows Let's Go
dropped), so Let's Go trees are kept only on FireRed blocking tiles, Let's Go
encounter grass is removed, and the slice dresses FireRed trees / ledges / grass with
kit pieces standing on the Let's Go heightmap.
"""
import os, sys, json, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, bntx, gf2glb, gfbmdl, terrain

PALLET = (-8.86, 0.09, 0.01)
MAPS = {
    'pallet': dict(
        area='area02', root=PALLET, overlay=False,
        models=[('town001_00', None), ('sea_00', (-2500, 6000, -500, 4500))],
        moves=[
            (1350, 1950, 250, 700, -60, 0),   # player house -> FireRed (5..9, 4..6)
            (2150, 2750, 250, 700, 44, 0),    # rival house  -> FireRed (14..18, 4..6)
        ]),
    'route1': dict(
        area='area02', root=(PALLET[0], PALLET[1] + 40, PALLET[2]), overlay=True,
        models=[('road001_00', None)], moves=[],
        drop_materials=('grass_s03', 'grass_s04', 'bm_signboard')),
}

def fr_map(mid):
    w = json.load(open(os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'world.json'), encoding='utf8'))
    return [m for m in w['maps'] if m['id'] == mid][0]

def main(romfs, out, work):
    field = os.path.join(romfs, 'bin', 'archive', 'field')
    os.makedirs(out, exist_ok=True)
    os.makedirs(work, exist_ok=True)
    manifest = {}
    for mid, cfg in MAPS.items():
        area = cfg['area']
        fm = fr_map(mid)
        texdir = os.path.join(work, 'tex', area)
        if not os.path.isdir(texdir):
            for _, d in gfpak.read(os.path.join(field, area + '.gfpak')):
                if d[:4] == b'BNTX':
                    bntx.export(d, texdir)
        entries = []
        heights = None
        for i, (name, clip) in enumerate(cfg['models']):
            files = gfpak.read(os.path.join(field, area, name + '.gfpak'))
            mdl = max(files, key=lambda f: len(f[1]))[1]
            tmp = os.path.join(work, name + '.gfbmdl')
            open(tmp, 'wb').write(mdl)
            m = gfbmdl.load(mdl)
            drop = None
            kept_trees = []
            if cfg['overlay']:
                cull, trunks, keep = terrain.tree_filter(m, cfg['root'], fm['tiles'])
                bad = cfg.get('drop_materials', ())
                drop = lambda n, c, cull=cull, bad=bad: any(b in n for b in bad) or cull(n, c)
                print(mid, name, 'trees kept %d / %d' % (sum(keep), len(keep)))
                tx, tz, s = cfg['root']
                kept_trees = [[round(x * s + tx, 2), round(z * s + tz, 2)] for (x, z), k in zip(trunks, keep) if k]
            dst = os.path.join(out, '%s_%d.glb' % (mid, i))
            info = gf2glb.convert(tmp, texdir, dst, moves=cfg['moves'], root=cfg['root'], clip=clip, drop=drop)
            entries.append(dict(path='letsgo/%s_%d.glb' % (mid, i), source='bin/archive/field/%s/%s.gfpak' % (area, name),
                                sha256=hashlib.sha256(open(dst, 'rb').read()).hexdigest(), **info))
            if i == 0:
                res, grid = terrain.heightmap(m, cfg['root'], len(fm['tiles'][0]), len(fm['tiles']))
                heights = dict(res=res, width=len(fm['tiles'][0]), height=len(fm['tiles']), grid=grid, trees=kept_trees)
                json.dump(heights, open(os.path.join(out, mid + '_height.json'), 'w'))
            print(mid, name, info)
        manifest[mid] = dict(scenes=entries, overlay=cfg['overlay'], heights='letsgo/%s_height.json' % mid)
    json.dump(manifest, open(os.path.join(out, 'scenery.json'), 'w'), indent=1)

if __name__ == '__main__':
    work = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work')
    main(sys.argv[1], sys.argv[2], work)
