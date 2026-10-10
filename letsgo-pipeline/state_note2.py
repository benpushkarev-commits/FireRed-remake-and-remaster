p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\docs\STATE.md'
t = open(p, encoding='utf8').read()
note = """## Let's Go world pass 2 (2026-10-10)

- Trainers rebuilt from Ben's RomFS (`tools/letsgo/import_letsgo_trainers.py`, `char2glb.py`):
  Red + 10 NPC models now carry original skeletons and real Let's Go field clips
  (fi wait/walk/run; picked by Origin travel, root motion stripped). Scale 0.01 = Let's Go
  proportions. Old GLBs kept as `assets/models/<key>/source/<key>_previous.glb`.
  `clip_speed` in slice-models.json picks Walk vs Run and sets playback rate so feet match.
  Character GLBs need V flipped (field ones don't). gfpak names are hashed (not FNV/xxh64).
- Seamless outdoors: rebuild spawns connected outdoor maps (2 links deep) at their
  connection offsets; crossing re-bases coordinates via `WorldShift` so camera/models carry
  over. Let's Go scenes persist across rebuilds (`LetsGoScene`). Doors still change map.
- Let's Go kit (`tools/letsgo/import_letsgo_kit.py` -> assets/letsgo/kit): tree split from
  Pallet's cluster, encounter grass, flowers, cut tree, boulder, grass/path/cliff/rock
  textures. Maps without a matching Let's Go layout (routes) keep FireRed tiles but use kit
  pieces/textures; Let's Go Route 1 layout differs from FireRed so it is not used.
- Camera: trailing look-at, boom snaps in / eases out, 60 deg FOV, pitch 0.05-1.2,
  canopy-height collision 3.6. Player model turns smoothly. Block plates hidden under models.
- Effects (Settings K / `effects`, default 1): HDR + ACES tonemap, subtle bloom, FXAA,
  outdoor distance haze, 3-cascade sun shadows, wind sway on kit foliage. Cargo features
  `bevy_post_process`, `bevy_anti_alias` added (Cargo.lock updated).
- QA: `--at <x> <z> <yaw>` places the free-move player for captures.
- Release build + 73 checks + packaged check/field/scene smoke passed; deployed, exe SHA256
  3A2409444A0F32D18B71548C0C153DEC28E1F1EA175A78DCBD16A3B9E5D7B504, native save unchanged.
- Next: Viridian/other towns from Let's Go where layouts match (fit with tools/letsgo
  render_top/overlay), Let's Go building models for FireRed houses, fence kit piece,
  mipmaps for distant textures, interiors.

"""
marker = "## Let's Go Pallet Town scenery (2026-10-10)"
if "## Let's Go world pass 2" not in t:
    t = t.replace(marker, note + marker, 1)
    open(p, 'w', encoding='utf8').write(t)
print('ok')
