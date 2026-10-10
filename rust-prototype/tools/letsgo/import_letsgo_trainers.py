"""Rebuild the slice trainer models straight from Ben's local Let's Go RomFS.

usage: python import_letsgo_trainers.py <Let's Go RomFS root>
Each key in assets/models/slice-models.json that has a Let's Go trainer pack is
re-exported with the original skeleton and Idle/Walk/Run field clips (char2glb).
The previous GLB is kept as <key>/source/<key>_previous.glb. Models use the same
0.01 scale as the Let's Go scenery so proportions match the world.
"""
import os, sys, json, shutil, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import char2glb

PACKS = {
    'red': 'tr0022_00_red', 'blue': 'tr0023_00_green', 'brock': 'tr0008_00_rock',
    'youngster': 'tr0027_00_shortpants', 'camper': 'tr0034_00_boyscout', 'woman': 'tr0063_00_adultf',
    'fat_man': 'tr0066_00_fat', 'scientist': 'tr0086_00_nresearcher', 'clerk': 'tr0079_00_staffm',
    'girl': 'tr0084_00_juveniles', 'old_man': 'tr0064_00_oldm',
}

def main(romfs):
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    models = os.path.join(root, 'assets', 'models')
    manifest_path = os.path.join(models, 'slice-models.json')
    manifest = json.load(open(manifest_path, encoding='utf8'))
    for entry in manifest:
        pack = PACKS.get(entry['key'])
        if not pack:
            continue
        src = os.path.join(romfs, 'bin', 'archive', 'chara', 'data', 'tr', pack + '.gfpak')
        dst = os.path.join(models, entry['file'].split('/', 1)[1])
        tmp = dst + '.new'
        try:
            info = char2glb.convert(src, tmp)
        except Exception as e:
            print(entry['key'], 'kept previous model:', e)
            if os.path.exists(tmp): os.remove(tmp)
            continue
        keep = os.path.join(os.path.dirname(dst), 'source', entry['key'] + '_previous.glb')
        os.makedirs(os.path.dirname(keep), exist_ok=True)
        if os.path.exists(dst) and not os.path.exists(keep):
            shutil.copy2(dst, keep)
        os.replace(tmp, dst)
        entry.update(letsgo_pack='bin/archive/chara/data/tr/%s.gfpak' % pack, clip_speed=[info['native_speed'][c] for c in ('Idle', 'Walk', 'Run')],
                     height=None, sha256=hashlib.sha256(open(dst, 'rb').read()).hexdigest())
        entry.pop('height')
        print(entry['key'], info)
    json.dump(manifest, open(manifest_path, 'w', encoding='utf8'), indent=2)

if __name__ == '__main__':
    main(sys.argv[1])
