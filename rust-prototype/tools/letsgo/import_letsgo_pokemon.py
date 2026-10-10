"""Rebuild the slice Pokemon models from Ben's local Let's Go RomFS (bin/archive/pokemon).

usage: python import_letsgo_pokemon.py <Let's Go RomFS root>
Clips: Idle (battle wait), Walk/Run (field), Attack (physical), Special, Hurt, Faint, FieldIdle.
Sizes keep the manifest preview heights; previous GLBs stay in <key>/source/<key>_previous.glb.
"""
import os, sys, json, shutil, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, char2glb, anim_names

DEX = {'bulbasaur': 1, 'charmander': 4, 'squirtle': 7, 'pidgey': 16, 'rattata': 19}
HEIGHT = {'bulbasaur': 0.7, 'charmander': 0.85, 'squirtle': 0.72, 'pidgey': 0.5, 'rattata': 0.45}

def main(romfs):
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    models = os.path.join(root, 'assets', 'models')
    path = os.path.join(models, 'slice-models.json')
    manifest = json.load(open(path, encoding='utf8'))
    for entry in manifest:
        key = entry['key']
        if key not in DEX:
            continue
        pack = os.path.join(romfs, 'bin', 'archive', 'pokemon', 'pm%04d_00.gfpak' % DEX[key])
        files = gfpak.read(pack)
        named = anim_names.resolve(files)
        p = lambda *k: anim_names.pick(named, *k)
        clips = {
            'Idle': p('ba10_waitA', 'ba10_wait', 'fi01_wait'),
            'Walk': p('fi20_walk', 'fi21_run'),
            'Run': p('fi21_run', 'fi20_walk'),
            'Attack': p('ba20_buturi'),
            'Special': p('ba21_tokusyu', 'ba20_buturi'),
            'Hurt': p('ba30_damage'),
            'Faint': p('ba41_down'),
            'FieldIdle': p('fi01_wait', 'kw01_wait'),
        }
        missing = [k for k in ('Idle', 'Walk', 'Run') if clips[k] is None]
        if missing:
            print(key, 'kept previous model, missing clips', missing, sorted(named)[:8]); continue
        dst = os.path.join(models, entry['file'].split('/', 1)[1])
        tmp = dst + '.new'
        info = char2glb.convert(pack, tmp, clips=clips, target_height=HEIGHT[key])
        keep = os.path.join(os.path.dirname(dst), 'source', key + '_previous.glb')
        os.makedirs(os.path.dirname(keep), exist_ok=True)
        if os.path.exists(dst) and not os.path.exists(keep):
            shutil.copy2(dst, keep)
        os.replace(tmp, dst)
        entry.update(letsgo_pack='bin/archive/pokemon/pm%04d_00.gfpak' % DEX[key], clips=info['clips'],
                     clip_speed=[info['native_speed'].get(c, 0) for c in ('Idle', 'Walk', 'Run')],
                     sha256=hashlib.sha256(open(dst, 'rb').read()).hexdigest())
        print(key, info['clips'], 'scale', info['scale'])
    json.dump(manifest, open(path, 'w', encoding='utf8'), indent=2)

if __name__ == '__main__':
    main(sys.argv[1])
