p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\docs\STATE.md'
t = open(p, encoding='utf8').read()
note = """## Let's Go Pallet Town scenery (2026-10-10)

Pallet Town in the Rust slice now renders Ben's local Let's Go field model
(`bin/archive/field/area02/town001_00.gfpak` + area02 textures + nearby `sea_00`)
instead of the FireRed block scenery. Tiles, collision, props, warps, NPCs and
scripts stay FireRed-authoritative; only cosmetic blocks are replaced. Settings
L / `letsgo_scenery` (default 1) switches back to blocks; missing
`assets/letsgo/scenery.json` also falls back. Let's Go units are 100 per tile:
root transform (-8.86, 0, 0.09) scale 0.01; player/rival houses shifted onto their
FireRed footprints, lab already matches. Water channel sits ~0.7 tile west of
FireRed's water tiles (accepted). Importer: `rust-prototype/tools/letsgo/`
(pure Python gfpak/BNTX/gfbmdl -> GLB, no Toolbox/Blender needed):
`python tools/letsgo/import_letsgo_field.py <RomFS root> assets/letsgo`.
Field shaders approximated: ground split grass/soil by vertex colour, cliffs by
normal, ramp-lit trees baked, shadow-only meshes and sky dropped, water flat colour.
Release build: core check, 73 library checks, packaged --check, field/scene smoke
passed; deployed to `build 1 pokemon fire` (exe SHA256
94972674A544E80873DF553CEF89AC2422B40CFDC090290F49FE826216DB8441), native save
unchanged. Captures: `rust-prototype/captures/letsgo-*.png`. Next: Route 1
(`area02/road001_00`), Pallet interiors (`t001r01..03`), real trainer clips
from `.gfbanm`.

"""
marker = '## Rust collaboration (2026-10-10)'
if note.splitlines()[0] not in t:
    t = t.replace(marker, note + marker, 1)
    open(p, 'w', encoding='utf8').write(t)
print('ok')
