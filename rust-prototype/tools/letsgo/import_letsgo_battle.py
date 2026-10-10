"""Let's Go battle stages (bin/archive/background) -> assets/letsgo/battle/<stage>.glb

usage: python import_letsgo_battle.py <Let's Go RomFS root> <out assets dir> [stage ...]
Stages are centred on the battle origin at 0.01 scale; battle.json lists them with the
FireRed map ids that use each one. btl_N_* are the full trainer-battle arenas; btl_F_*
are only rings meant to surround the overworld, so they are not used.
"""
import os, sys, json, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, bntx, gf2glb

STAGES = {
    'btl_N_grass': ['route1', 'route2', 'route22', 'viridian_forest'],
    'btl_N_road': ['pallet'],
    'btl_N_tokiwa': ['viridian'],
    'btl_N_nibi': ['pewter'],
    'btl_sky': [],
}

def convert_stage(romfs, name, dst):
    src = os.path.join(romfs, 'bin', 'archive', 'background')
    work = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work')
    files = gfpak.read(os.path.join(src, name + '.gfpak'))
    texdir = os.path.join(work, 'tex', name)
    if not os.path.isdir(texdir):
        for _, d in files:
            if d[:4] == b'BNTX':
                bntx.export(d, texdir)
    mdl = max((d for _, d in files if d[:4] == b'\x20\x00\x00\x00'), key=len)
    tmp = os.path.join(work, name + '.gfbmdl')
    open(tmp, 'wb').write(mdl)
    return gf2glb.convert(tmp, texdir, dst, root=(0.0, 0.0, 0.01), sky=True)

def main(romfs, out, only=()):
    dst_dir = os.path.join(out, 'battle')
    os.makedirs(dst_dir, exist_ok=True)
    if only:
        for name in only:
            print(name, convert_stage(romfs, name, os.path.join(dst_dir, name + '.glb')))
        return
    manifest = {}
    for name, maps in STAGES.items():
        dst = os.path.join(dst_dir, name + '.glb')
        info = convert_stage(romfs, name, dst)
        manifest[name] = dict(path='letsgo/battle/%s.glb' % name, maps=maps,
                              sha256=hashlib.sha256(open(dst, 'rb').read()).hexdigest(), **info)
        print(name, info)
    json.dump(manifest, open(os.path.join(dst_dir, 'battle.json'), 'w'), indent=1)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
