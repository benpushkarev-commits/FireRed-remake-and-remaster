# FireRed Remaster — project state

Hand-off note between sessions and tools. Update it when something changes.
Last updated: 2026-10-10 (separate Rust prototype; installed C++ game unchanged).

## Let's Go Route 1 + Pokemon models (2026-10-10)

- Route 1 renders Let's Go `area02/road001_00` (same area frame as Pallet, root shifted by the FireRed connection (0,-40)). `overlay` maps keep FireRed gameplay: Let's Go trees survive only on FireRed X tiles (155/206), Let's Go encounter grass + sign removed, and kit trees/grass/ledges dress FireRed features. `tools/letsgo/terrain.py` rasterises Let's Go ground into `assets/letsgo/<map>_height.json` (4 samples/tile, holes filled); player, NPCs, kit pieces and the camera floor stand on it (Route 1 rises ~3 tiles north). Camera boom now only stops on X tiles/buildings and rides over terrain.
- Pokemon battle models rebuilt from `bin/archive/pokemon/pm####_00.gfpak` (`import_letsgo_pokemon.py`): normal (not _rare) model, real clips Idle/Walk/Run/Attack/Special/Hurt/Faint/FieldIdle. Clip names recovered by `anim_names.py` (files are in name order; eye/mouth face clips anchor alignment). Pokemon/trainer UVs need V flip; eye atlas uses ColorUVScale/Translate. Battle previews play Attack when the foe loses HP, Hurt when hit, Faint at 0 HP.
- Deployed exe SHA256 6F3F11AB5302BA0779CA527708EEC06E4669F304A82DBE7EA2127E5DC39C2141; checks passed, save unchanged. Next: 3D battle scene on the field, Charmander flame material, Viridian Let's Go map.

## Playable slice quick start (2026-10-10)

Settings Y / `quick_start` (default 1): with an empty party the HUD asks for a starter (1 Bulbasaur, 2 Charmander, 3 Squirtle). `src/slice_start.rs` gives it at Lv5 (source CreateMon path), 5 Poke Balls + 3 Potions, FLAG_SYS_POKEMON_GET/POKEDEX_GET and the post-Pokedex scene vars (0x4050=1, 0x4055=6, 0x4057=2, 0x4051=1, 0x4058=1, 0x4054=1) plus lab hide flags, so Oak's escort/lab/parcel story is skipped. Route 1 grass encounters, wild battles, catching, fleeing and whiteout use the existing native systems (needs field_scripts=1 + battle_slice=1, already in Ben's config). UI text drops accents the font lacks. QA: `--route --at 6 33 1.5708 --slice-auto --field-scripts --capture <png>` walks the grass until a wild battle (Pidgey Lv4 after ~1700 frames) and captures it. Deployed exe SHA256 5A704686BB820007C9E3B0A86D7D3E4D5F8A8299F36C7474683ABFD7448755DE.

## Let's Go world pass 2 (2026-10-10)

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

## Let's Go Pallet Town scenery (2026-10-10)

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

## Rust collaboration (2026-10-10)

Playable-slice model/animation pass DEPLOYED to Ben's supplied
`C:/Users/benpu/Desktop/build 1 pokemon fire/FireRed Native 3D Prototype 0.1.exe`.
SHA256 `09542D9AFFF76286B38F286B1EDC5B458C65B30BCCB9ECF4EC7336287EC51FDC`.
Full glTF scenes now retain humanoid skins; basic authored Idle/Walk/Run clips
crossfade with actual movement and pause in Settings. Imported Red plus 10 matching
NPC models (Blue/Brock/Youngster/Camper/woman/fat man/scientist/clerk/girl/old man).
Source actor IDs, visibility, movement and collision remain authoritative; Blue's
scripted scenes require existing actor_scenes. Unsupported actors retain blocks.
Five Pokemon (Bulbasaur/Charmander/Squirtle/Pidgey/male Rattata) have live 3D battle
portraits with planted root idle sway; original skeletal/attack/faint clips absent.
Pokemon previews use Collada meshes/materials; source rigs remain in the archives.
Bulbasaur attack vines excluded from idle; Charmander flame has orange emission.
Tab then A/T/P toggles character_animation/trainer_models/pokemon_models; config.ini
keys saved, default1. V still toggles Red. Music remains0, no zoom setting.
Importer/checker: rust-prototype/tools/{import_slice_models,check_slice_models}.py;
runtime: src/models.rs. FBX root pose and left-arm export basis corrections are
covered by independent decoded skin/loop/arm checks. All16 model checks,73 library
checks, core startup, packaged --check/field-smoke/scene-smoke and 131-move free
replay passed. Fresh Red idle/walk/run, NPC, Pokemon, battle and Settings captures
inspected silently. Final character capture: slice-models-character-accepted.png.
Installed slice --check passed; all executable/model hashes match. Native save
unchanged `EA8F10B99E11B1692ADA257ED1471A75D70260DCD9D9A73BE1A91A3B454809CF`;
existing data identical, old exe/config/save/Red model backed up inside slice/backups.
Installed C++ Pokemon Fire Red.exe unchanged. These motions are placeholders,
not extracted original-game clips. Prefer matching Let's Go trainer rig clips
for a later animation pass; Pokemon assets span Scarlet/Violet and X/Y rigs.

Latest Codex self-effect package: Recover restores half maximum HP with source floor,
minimum and full-HP failure order; Defense Curl sets its persistent volatile marker
before the Defense-stage result; Water Sport sets the per-user marker without an
accuracy draw and globally halves Fire move power while present. Teleport now spends
PP before trainer/trap failure and ends eligible wild battles without the ordinary
speed/run-attempt RNG, preserving the source `IsRunningFromBattleImpossible` gates
for the represented volatile traps, Rooted, Shadow Tag, Arena Trap, Magnet Pull,
Levitate/Flying and Run Away/Smoke Ball override. The same serialized release also
compiled and packaged Gemini's stable Fuchsia/Safari handoff: 180 maps, 186 source
textures, 751 actor objects and 102 triggers. Core startup, all 73 library checks,
packaged `--check`, field smoke and scene smoke passed. Output EXE SHA256 is
`EA300BD7EE714C2EBCE6071A23504AC4EE47063802903EB7B99872F6971F26B3`; no pending
EXE, native music remains 0, installed C++ game untouched. Rollout consumption of
the Defense Curl bit, party switching/lifecycle coverage, Ghost-without-Scope battle
state and exact ability-specific Teleport presentation remain gaps. New behavior is
UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Next Codex source-mechanics batch is the remaining curated residual interaction:
Leech Seed, Wrap/trapping and Rapid Spin cleanup. Gust's airborne multiplier and Low
Kick's weight power follow after the missing semi-invulnerable/weight state is sourced.

Latest Codex imported damage-effects package: Swift now explicitly bypasses accuracy
stages without an accuracy draw; Karate Chop uses the source one-in-eight critical
rate and Battle Armor/Shell Armor short-circuit before critical RNG. Absorb and Leech
Life restore half the actual HP dealt with source integer rounding and cap, while
Liquid Ooze reverses that drain and can faint the attacker. Bubble and Rock Tomb use
their imported inclusive secondary chances and source stat-drop gates; Rock Tomb's
certain effect bypasses Shield Dust/Clear Body/White Smoke as in `ChangeStatBuffs`.
Ordinary integrated trainer/wild battles now start past Oak's tutorial gates, while
the three lab rival fixtures retain them. Serialized release build/package passed
core startup, all 72 library checks, packaged `--check`, field smoke and scene smoke.
Output EXE SHA256 is
`F8362153B3715F42E4F48A00DCC7F39E35F6BD5C842734FCCA6AC8FE4C9A81EF`; no pending
EXE, native music remains 0, installed C++ game untouched. Substitute HP, Mist,
semi-invulnerable targets and the remaining critical-stage items/statuses are gaps.
New behavior is UNVERIFIED by a dedicated original-C comparison; source inspection
and basic checks only.

Latest Codex freeze/thaw package: damaging freeze effects now follow the source
chance and prevention order through Shield Dust, Safeguard, faint/Substitute, Ice
typing, existing primary status and Magma Armor. A frozen battler makes the source
one-in-five thaw roll after sleep and before confusion/paralysis; a blocked turn uses
no PP and preserves last-move state. `EFFECT_THAW_HIT` consumes that roll and clears
freeze only at the later canceller stage, while a surviving target thaws after a
nonzero effective Fire hit. Serialized release build/package passed core startup, all
71 library checks, packaged `--check`, field smoke and scene smoke. Output EXE SHA256
is `A4586627C34989EF913E3A5D716150024505A692DFACA8C173E7D7347881C94A`; no pending
EXE, native music remains 0, installed C++ game untouched. The bounded battle state
does not yet represent weather, so harsh-sun freeze prevention is deferred. Current
curated battle data also exposes no freeze-hit or thaw-hit move to regular fixtures.
New behavior is UNVERIFIED by a dedicated original-C comparison; source inspection
and basic checks only.

Latest Codex Safeguard package: battle state now carries source side-wide Safeguard
counters. The effect sets five end-turn phases, refuses a duplicate without refreshing
the timer, protects either side, expires after the fifth phase, and has an executor
path for `EFFECT_SAFEGUARD`. Existing damaging poison, burn, paralysis and confusion
secondaries consume their chance RNG before Safeguard rejects them, preserving
Shield Dust then Safeguard then faint/Substitute source order. Sing performs its
Safeguard check after accuracy and still spends PP. Serialized release build/package
passed core startup, all 70 library checks, packaged `--check`, field smoke and scene
smoke. Output EXE SHA256 is
`65E5C079D809B26BF40A2CC8C0060FDD9C115F4FCF363EA9FDF22619D352C315`; no pending EXE,
native music remains 0, installed C++ game untouched. The curated battle move data
does not yet expose `MOVE_SAFEGUARD` to current fixtures; broad primary-status scripts,
ability-caused status and Safeguard AI selection remain gaps. New behavior is
UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Latest Codex confusion package: the damaging Confusion move now applies its source
10% secondary-effect path, including the inclusive chance draw, Serene Grace, Shield
Dust, Substitute, Own Tempo, existing-confusion and faint/no-effect rejection. The
volatile two-to-five-turn counter decrements before action; an active turn performs
the source half-chance draw and 40-power self-damage using the user's own Attack and
Defense, while a zero counter snaps out and permits the selected move. Confusion can
coexist with primary status. Serialized release build/package passed core startup,
all 69 library checks, packaged `--check`, field smoke and scene smoke. Output EXE
SHA256 is `9B6816F9FC030FF79E1FBBF4D4BE05ED0D211C27527CFE6E7001A5FDAEAABC8E`;
no pending EXE, native music remains 0, installed C++ game untouched. Primary
confusion moves, confusion-curing items, freeze and broader volatile
status handling remain gaps. New behavior is UNVERIFIED by a dedicated original-C
comparison; source inspection and basic checks only.

Latest Codex sleep package: Sing now follows its source target-check/accuracy order,
including Substitute, existing status, Insomnia, Vital Spirit, Uproar and Soundproof
prevention, then assigns the source two-to-five counter. Sleeping battlers decrement
before acting and use no PP while asleep; Early Bird decrements twice, while Uproar
wakes non-Soundproof battlers and clears Nightmare. Serialized release build/package
passed core startup, all 68 library checks, packaged `--check`, field smoke and scene
smoke. Output EXE SHA256 is
`EEA6FAB55FB3DA96CA33E1ECA0C4A6CF786B1C7351B4829098360FC3C8BD552A`; no pending EXE,
native music remains 0, installed C++ game untouched. Safeguard side state, Sleep Talk,
Snore, freeze, confusion and broader status/ability handling remain gaps. New behavior
is UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Latest Codex paralysis package: Thunder Shock now applies its imported secondary
chance through the source-ordered handler, including Serene Grace and Limber/Shield
Dust/existing-status/no-effect rejection. Paralysis quarters staged Speed and makes
the source one-in-four action check before accuracy, announcement and PP use; a fully
paralyzed battler spends no PP and retains its prior last move. Standalone enemy turns
now reach residual status processing after misses or full paralysis. Serialized
release build/package passed core startup, all 67 library checks, packaged `--check`,
field smoke and scene smoke. Output EXE SHA256 is
`80A7EFDD17D074BCA1FAD9C8AAA1E34F37CDD94D9DAA2059F54DEA07A2A46ABD`; no pending EXE,
native music remains 0, installed C++ game untouched. Sleep, freeze, confusion,
Safeguard/substitute and broader status/ability handling remain gaps. New behavior is
UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Latest Codex burn package: Ember now uses the imported source secondary chance and
FireRed's `<=` RNG comparison, with source ability selection plus Fire type, Water
Veil, Shield Dust and existing-status prevention. Serene Grace doubles the chance.
Burn applies max HP / 8 end-turn damage and halves only physical pre-+2 base damage;
Guts bypasses the penalty. Poison Sting now shares the generalized secondary-effect
path. Serialized release build/package passed core startup, all 66 library checks,
packaged `--check`, field smoke and scene smoke. Output EXE SHA256 is
`39D1AE173E39DB05FC3F846C127C6EFAD38FD1D71126AD5B70DEA295EDD72E07`; no pending EXE,
native music remains 0, installed C++ game untouched. Paralysis, sleep, freeze,
confusion, Safeguard/substitute and broader status/ability handling remain gaps. New
behavior is UNVERIFIED by a dedicated original-C comparison; source inspection and
basic checks only.

Latest Codex type-damage package: bounded battles now apply FireRed's same-type attack
bonus and complete Generation III type chart after base damage, preserving source
integer order, distinct dual-type processing, cancellation and zero-damage immunities.
Actual attacks and AI faint checks share the typed result, with source-style
effectiveness messages. Serialized release build/package passed core startup, all 65
library checks, packaged `--check`, field smoke and scene smoke. Output EXE SHA256 is
`C56EA71FB814CECC3CA2426C6156965ABB91D847C045A1BAFD77362E30531A0B`; no pending EXE,
native music remains 0, installed C++ game untouched. Levitate, Wonder Guard,
Foresight, dynamic move/type changes, Struggle and the remaining damage modifiers are
explicit gaps. New behavior is UNVERIFIED by a dedicated original-C comparison;
source inspection and basic checks only.

Latest Codex damage package: FireRed's type-based physical/special split now selects
Attack/Defense for Normal through Steel and Special Attack/Special Defense for Fire
through Dark. Both special stages are represented, and critical-hit stage bypass uses
the same source rules on either branch. Existing exact move-type metadata is consumed
read-only. Serialized build/package passed core startup, all 63 library checks,
packaged `--check`, field smoke and scene smoke. Output EXE SHA256 is
`FB4BC45DA99431623D049F81F4AFE03DC3BD6A7F1CC3A954D3D46660DE7B4C64`; no pending EXE,
native music remains 0, installed C++ game untouched. STAB, type effectiveness, burn,
items, badges, abilities, weather, screens and doubles remain damage gaps. New behavior
is UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Latest Codex accuracy package: Sand Attack now lowers the target's real Accuracy stage,
and hit checks use FireRed's 13-entry Accuracy/Evasion ratio table with source integer
order and RNG consumption. The AI VM supports Accuracy/Evasion queries and the
persistent/volatile status predicates required by Pidgey's source scoring. A focused
check forces a Route 1 Pidgey Sand Attack and verifies the resulting 75% threshold.
Serialized build/package passed core startup, all 62 library checks, packaged
`--check`, field smoke and scene smoke. Output EXE SHA256 is
`90D30D53F2EEB9D28EEF95BA87B3D32E3C174E05BA14FA31F8F29AAC4301C50F`; no pending EXE,
native music remains 0, installed C++ game untouched. Foresight, accuracy/evasion
items and abilities, weather, Protect and other stage moves remain gaps. New behavior
is UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Latest Codex battle-AI package: the bounded VM now reads exact species/move types from
the existing source-backed core import, records last used moves and executes the source
last-move power/type, battler-type and byte-list membership commands. This closes the
opponent Harden scoring gap without modifying Gemini-owned importers or generated data.
The focused check runs two opponent Metapod turns across 256 deterministic seeds.
Serialized build/package passed core startup, all 61 library checks, packaged
`--check`, field smoke and scene smoke. Output EXE SHA256 is
`D5593918B1D3AABA9A9FAE15F6EE0B0709F0096CAFE63F84A1CA80A73D10410C`; no pending EXE,
native music remains 0, installed C++ game untouched. Full type effectiveness,
physical/special damage separation, switching history and many AI commands remain
gaps. New behavior is UNVERIFIED by a dedicated original-C comparison; source
inspection and basic checks only.

Latest Codex Harden package: the bounded battle executor now applies Harden's source
one-stage Defense increase, maximum stage 12 result and staged physical damage effect.
Imported accuracy-zero moves follow their scripts without an accuracy check or RNG
draw. AI HP-percent equal/not-equal branches are also supported. A focused check
verifies all of those mechanics. Serialized build/package passed core startup, all 61
library checks, packaged `--check`, field smoke and scene smoke. Output EXE SHA256 is
`D58F4AC3D6C4B47CC9868B0A6398BFDD72B0BE07D98334798E7F639660DC5196`; no pending EXE,
native music remains 0, installed C++ game untouched. The following battle-AI package
closes the opponent last-move/type selection gap; other stat-up effects and special
damage remain gaps. New behavior is UNVERIFIED by a dedicated original-C comparison;
source inspection and basic checks only.

Latest Codex battle-speed package: String Shot now lowers the target's Speed stage
through the bounded trainer battle interpreter, including the minimum-stage result.
Move order applies source stat-stage ratios and paralysis quartering after move
priority. The needed AI target-speed comparison shares that calculation and retains
the source equal-Speed RNG draw. A focused check forces Sammy's supported move path
and verifies the resulting order change. Serialized build/package passed core
startup, all 60 library checks, packaged `--check`, field smoke and scene smoke.
Output EXE SHA256 is
`07A5C7488C506B8F659E3039BD44604DAB5F26529549C19289EC72BBF7E62336`; no pending EXE,
native music remains 0, installed C++ game untouched. Badge, held-item, weather and
ability Speed modifiers remain gaps. New behavior is UNVERIFIED by a dedicated
original-C comparison; source inspection and basic checks only.

Latest Codex battle-status package: the bounded engine now ports Poison Sting's source
secondary-effect RNG draw and `<= 30` chance, with imported Poison/Steel/Immunity
protection for integrated party Pokémon. End-turn regular poison uses max HP / 8;
toxic poison uses max HP / 16 and the capped 1-15 counter. Residual faints follow the
same outcome and single-participant experience/EV path as attack KOs, and status returns
to persistent party state. Other status moves and broad ability/Safeguard/substitute
interactions remain gaps. Serialized build/package passed core startup, all 59 library
checks, packaged `--check`, field smoke and scene smoke. Output EXE SHA256 is
`CFF3E9E9159402EC15CFB4034A5B288BCEA3BBF43718256F13C06EA4B13D45E5`; no pending EXE,
native music remains 0, installed C++ game untouched. New behavior is UNVERIFIED by a
dedicated original-C comparison; source inspection and basic checks only.

Latest Codex field-step package: ordinary, non-forced native walking now advances the
persistent source poison counter modulo five. Every wrap removes one HP from each
regular/toxic-poisoned party Pokémon. Poison fainting clears status and applies the
source friendship tier loss; any faint preempts same-step Repel/encounter processing.
An all-party poison faint uses the existing source money/heal/checkpoint whiteout path.
Source-named Secret Base maps are excluded. Exact nickname text and the poison
palette/sound effect remain presentation gaps. Packaged `--check` forces fatal poison
and verifies counter, friendship, money, healing, dialogue and checkpoint recovery.
Serialized build/package passed core startup, all 58 library checks, packaged
`--check`, field smoke and scene smoke. Output EXE SHA256 is
`DC7068ECEFD630BA1677B0D273DAB939BB8B29C032293912BA65B4A096AB688F`; no pending EXE,
native music remains 0, installed C++ game untouched. Happiness, Vs Seeker, egg and
Safari step counters remain gaps. New behavior is UNVERIFIED by a dedicated original-C
comparison; source inspection and basic checks only.

Latest Codex trainer-integrity package: source audit removed every shortened multi-mon
trainer fixture that could incorrectly complete a quest. The single-mon battle slice
now accepts only the complete Oak-lab rival teams (IDs 326-328) and Bug Catcher Sammy
(104); other trainers fail explicitly pending full party imports and switching.
Trainer battle start sets the opponent's persistent Pokédex seen bit. Victory awards
the exact source class/last-level prize (¥80 lab rival, ¥108 Sammy), doubled when the
participating Pokémon holds an Amulet Coin, through the existing capped money state.
Packaged `--check` verifies multi-mon rejection, seen registration, doubled ¥216 prize,
result and defeated flag. Serialized build/package passed core startup, all 57 library
checks, packaged `--check`, field smoke and scene smoke. Output EXE SHA256 is
`C55943C1609E78FBF862A1A7F36D6052BFB237E460999D76E68DBDF67F45265B`; no pending EXE,
native music remains 0, installed C++ game untouched. Full trainer-party import,
switching, prize dialogue and broad battle support remain gaps. New behavior is
UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks
only.

Latest Codex special-ball package: regular wild catches now port the source Net, Dive,
Nest, Repeat, Timer, Luxury and Premier Ball multipliers. Walking battles derive Net
context from imported source types and Repeat context from the persistent caught bit
before marking the encounter seen. Nest uses target level; Timer uses the source-capped
turn counter and retains its late `u8` addition wrap. FireRed supplies no underwater map
type, so Dive stays x1. Successful special-ball captures retain their ball metadata.
Safari Ball still fails explicitly pending Safari catch-factor battle setup, and the
current battle UI still offers only a Poke Ball pending original bag selection. Packaged
`--check` verifies uncaught then caught field context. Serialized build/package passed
core startup, all 56 library checks, packaged `--check`, field smoke and scene smoke.
Output EXE SHA256 is `22E60A9F85C3D44979178C619332736DF80665D35F1936125272A679FD121677`;
no pending EXE, native music remains 0, installed C++ game untouched. New behavior is
UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks only.

Latest Codex trainer-state package: the bounded field trainer adapter now preserves
the source trainer mode and rival flags, begins battles from the persistent global RNG
state and returns the exact final state/call count. It selects and synchronizes the first
usable party Pokemon on either outcome. Normal result values are source outcomes 1/2;
early-rival result values are FALSE/TRUE. Trainer flags are set after wins and after the
healed tutorial loss, but not after ordinary losses. `RIVAL_BATTLE_HEAL_AFTER` now uses
the source bit 0; ordinary/unhealed losses use the native money/heal/checkpoint whiteout.
Unknown teams and unusable parties fail explicitly instead of fabricating opponents or
player Pokemon, and integrated battles cannot restart into the isolated fixture.
Packaged `--check` exercises the Oak-lab healed-loss path, including
RNG/result/flag/heal/script assertions. Serialized build/package passed core startup,
all 54 library checks, packaged `--check`, field smoke and scene smoke. Output EXE SHA256
is `844FD1F0F653E4C0B08E3E2F5B3CC5CA50FB4C652FBC4A92692AABD995CDEAEA`; no pending
EXE, native music remains 0, installed C++ game untouched. Complete trainer-party data,
switching, prize money, broad battle support and trainer Pokédex registration remain
gaps. New behavior is UNVERIFIED by a dedicated original-C comparison; source
inspection and basic checks only.

Latest Codex Pokédex-state package: native saves now persist source-sized seen/owned
bitsets, National Dex enable markers, and first-seen Unown/Spinda personalities. The
full 411 internal-species to National Dex mapping is ported, including legacy Unown
slots and FireRed's non-linear Hoenn ordering. Walking wild battle start sets seen;
successful party/PC captures and script `givemon` set caught. `GetPokedexCount`,
`SetSeenMon`, `EnableNationalPokedex` and `IsNationalPokedexEnabled` now follow their
source state/results. Older native saves gain an empty Dex. Packaged `--check` covers
encounter/capture seen-to-caught transition. Serialized build/package passed core
startup, all 53 library checks, packaged `--check`, field smoke and scene smoke.
Output EXE SHA256 is `8AEFD870894D38F4B6B4BC2A7B86E7FA88FDD11EC41C189A4E5837751D6C3E87`;
no pending EXE, native music remains 0, installed C++ game untouched. Pokédex UI,
trainer-battle/evolution/trade registration and original redundant save-block anticheat
copies remain gaps. New behavior is UNVERIFIED by a dedicated original-C comparison;
source inspection and basic checks only.

Latest Codex whiteout package: losing an integrated walking wild battle now applies
the source badge-count/highest-level money-loss formula with the current-money cap,
fully heals party HP/status/PP, and uses the persisted `setrespawn` heal location.
Imported Pallet, Viridian, Pewter, Cerulean, Lavender, Vermilion, Celadon, Route 4
and Route 10 recovery destinations use the source whiteout coordinates; unimported
heal maps fail explicitly. Older native saves default to Pallet's checkpoint. Packaged
`--check` exercises loss to Viridian Center. Serialized build/package passed core
startup, all 52 library checks, packaged `--check`, field smoke and scene smoke.
Output EXE SHA256 is `4883AA8C690FE2B958C0183D3DEE90DFA8ABCAB94D1F96C2DC8485EB7A4E76A8`;
no pending EXE, native music remains 0, installed C++ game untouched. Recovery cutscene
timing/NPC scripts, overworld mode resets and unimported destinations remain gaps. New
behavior is UNVERIFIED by a dedicated original-C comparison; source inspection and
basic checks only.

Latest Codex RNG-continuity package: walking wild battles now begin from the
persistent `random.c` global RNG state after encounter selection/wild creation,
and resolution returns the battle's final state and exact call count to that same
stream. The previous extra two-draw `next32` battle seed has been removed. Packaged
`--check` now asserts start-state identity and post-capture state/call continuity.
Serialized build/package passed core startup, all 51 library checks, packaged
`--check`, field smoke and scene smoke. Output EXE SHA256 is
`9DDB1AA364210862ADCF79D395ADFC4A9D364C616314EC6F015372F930C61A8B`;
no pending EXE, native music remains 0, installed C++ game untouched. Trainer-battle
world integration and original per-frame background RNG remain gaps. New behavior is
UNVERIFIED by a dedicated original-C comparison; source inspection and basic checks only.

Latest Codex core package: native state now includes the source-sized 14 PC boxes
with 30 slots each, current box, default names and wallpapers. `GiveMonToPlayer` /
`SendMonToPC` semantics route full-party script gifts and captures from the current
box through the circular source scan, restore PP before boxing, update destination
box variables, and clear the box-full notice flag when the destination changes.
Captured-ball metadata now survives battle synchronization and native persistence;
older native JSON saves receive default empty storage and Poke Ball metadata. Ball
use is refused before consumption when party and storage are both full. Packaged
`--check` exercises a full-party capture into Box 1. Serialized build/package passed
core startup, all 51 library checks, packaged --check, field smoke and scene smoke.
Output EXE SHA256 is `0B53F64BF2C13459B63FB68F93192F139181C656FA65083877B21DC3311644F2`;
no pending EXE, native music remains 0, installed C++ game untouched. PC menus,
withdrawal/deposit/release, full BoxPokemon metadata/encryption and exact capture-result
message flow remain unported. New behavior is UNVERIFIED by a
dedicated original-C comparison; source inspection and basic checks only.

Latest Codex mechanics package: walking wild battles now use the pinned source's
`TryRunFromBattle` speed/retry rules, including Run Away and Smoke Ball, plus the
regular single-battle `Cmd_handleballthrow` HP/status odds, basic ball multipliers,
integer shake thresholds and RNG order (including the Master Ball quirk). The native
battle UI currently throws one Poke Ball from the persisted bag with C and consumes it;
failed throws give the wild opponent its turn. Unresolved wild battles can no longer
be cancelled or replaced with the rival fixture. Catch preserves the generated wild
identity and adds it to the party when space exists. Serialized build/package passed
core startup, all 49 library checks, packaged --check, field smoke and scene smoke.
Output EXE SHA256 is `95EAD9F8C820B937DB3F265140C81C35B84350D23B68EFF14963804B1A0B21C4`;
no pending EXE. Native output music is reset to 0. Installed C++ game remains untouched.
Special balls/context and battle bag selection remain gaps.
New behavior remains UNVERIFIED by a dedicated
original-C comparison; source inspection and basic checks only.

Latest combined native package: Celadon geography expanded the source-backed world
to 137 maps / 74,385 ROM-verified cells, 143 textures and 571 actor objects. Codex
integrated regular-land walking encounters using per-cell original encounter type,
private encounter RNG/cooldown/rate buff, main 16-bit slot/level draws, party-held
Cleanse Tag/lead ability/flutes, persisted Repel count and the original wore-off script.
Initial placement and map entry cannot roll; map entry/battle start reset immunity.
Wild Pokémon use source nature/personality/random-IV creation and captures retain the
created identity. Missing battle species/moves fail explicitly without substitution.
Serialized build/package passed core startup, 48 library checks, packaged --check
(actual walking/Repel/battle bridge), field smoke and scene smoke. Output executable
updated directly; installed C++ game untouched, audio silent. New behavior remains
UNVERIFIED against detailed original C/frame timing. Water, roamers, Unown/header
overrides, Tower ghosts/Safari setup, eggs/cycling/forced movement state, broad battle
support, PC capture transfer and faithful blackout remain missing.

Latest: Gemini completed Fuchsia City, Gym, and Safari Zone batch (180 maps,
103,723 cells ROM-verified) covering Fuchsia City (Gym, Move Deleter, Good Rod, Mart,
Center), and Safari Zone (Entrance, Office, Warden's House, Center/East/North/West areas,
all rest houses, and the Surf Secret House). Added secondary tilesets gTileset_FuchsiaGym
and gTileset_SafariZoneBuilding (186 baked textures), 751 actor objects, 102 triggers,
and maintained per-cell encounter_types ROM-verified across 103,723 cells. Generated data
is stable and validated 100% against World::validate() constraints in lib.rs. Codex-reserved
files remain untouched and cargo/build lock was strictly observed. Handoff ready for Codex
compilation and packaging.

Latest: Gemini completed Routes 7 & 8, Underground Path (East-West), Routes 16-18
(Cycling Road corridor), and Routes 12-15 (Silence Bridge corridor) batch (161 maps,
92,018 cells ROM-verified) connecting Celadon, Lavender, and southern Kanto. Added
secondary tileset gTileset_FuchsiaCity (167 baked textures), 680 actor objects, 99 triggers,
and maintained per-cell encounter_types ROM-verified across 92,018 cells. Generated data
is stable and validated 100% against World::validate() constraints in lib.rs. Codex-reserved
files remain untouched and cargo/build lock was strictly observed. Handoff ready for Codex
compilation and packaging.

Latest: Gemini completed Celadon geography batch (137 maps, 74,385 cells ROM-verified)
and added per-cell encounter_types to import_world.py/world.json from metatile attribute
bits 24..26 (0 none, 1 land, 2 water), byte-for-byte verified against USA rev0 ROM.
Celadon City, Gym, Dept Store 1F-5F/Roof/Elevator, Game Corner, Prize Room, Rocket Hideout
B1F-B4F/Elevator, Condominiums/Mansion 1F-3F/Roof/Roof Room, Hotel, Center, Restaurant, and
House 1 imported. 143 baked textures, 571 actor objects. Generated data stable.
Gemini avoided lib.rs, main.rs, field_screen.rs, encounter.rs, battle.rs, core.rs, and
did not run cargo/build.ps1. Handoff ready for Codex compilation and release packaging.

Latest: Gemini reports 93 imported maps after S.S. Anne / Route 11 / Diglett's Cave.
Codex added the regular-land step API (source cooldown, terrain-change gate,
separate RNG, Repel and buff/reset flow); 43 library tests pass. Walking integration
awaits field_screen.rs handoff (main.rs was offered, but is not the actual trigger).
Antigravity app approval timed out; next world assignment is recorded but not delivered.
Combined native dev package updated: build.ps1 exit 0, core startup and data/field/scene
smoke checks passed. Installed C++ game untouched.

Ben authorized Codex and Antigravity/Gemini to divide continued faithful Rust port work.
Current ownership and handoff protocol are in `docs/RUST-COLLABORATION.md`.
Gemini received the Route 5 through Vermilion world/import batch in its existing chat.
Codex reserves encounter.rs/battle.rs/core.rs and records progress in RUST-CODEX-STATUS.md.
A 30-minute thread heartbeat continues useful batches within account/tool limits.
First Codex batch corrects fixed-level encounter RNG consumption and reversed bounds;
adds original-order rate modifiers. All 40 library tests passed. The host still lacks
full encounter cooldown, dedicated RNG, repel/terrain gating and modifier integration.
Packaging waits for a stable Gemini batch; imported geography is not complete gameplay.

Follow-up: reviewed Gemini's 62-map Vermilion import batch and packaged the combined
native dev build (build.ps1 exit 0, core/data/field/scene smoke checks passed).
EncounterState now ports the separate encounter RNG, source cooldown arithmetic and
u16 encounter buff/Repel reset; host integration remains pending. 42 tests pass.
Gemini received the next S.S. Anne interior / Route 11 / Diglett's Cave assignment
and a review correction: trash-can special must use 16-bit Random consumption.

## Ben's install

### Native Rust save reset (2026-10-08)

Settings now offers R, then R again to confirm resetting all native Rust progress
at the current location. Clears core party/bag/events and exploration flags/items,
cancels current script/battle and writes a fresh native save. Previous save is
retained beside it as native-state.reset-<timestamp>.bak; installed C++ untouched.
Build and 31 existing library checks pass. Packaged --check verifies reset and
reload after receiving the Potion; field-smoke passes. Fresh Settings capture
checked for readable reset instruction. Standard executable replacement blocked
while Ben's game is open; updated build available as FireRed Native Reset Preview.exe.

### Native Rust original field VM batch (2026-10-08)

Ben confirmed save/load worked and authorized continuing. Pinned sparse
reference expanded with original asm macros/shared scripts/text/all map scripts.
tools/import_field.py assembles original event macros as native data, importing
128252 bytes,9731 labels,4580 conditional native texts,213 handler-table entries,
10 std scripts. Source hashes: data/field-provenance.json; runtime field.json.
Native archive offsets/import tokens and text payloads replace GBA pointers/
encoded text layout; no guest executable code or source-C runtime linkage.

src/field.rs ports context/stack, calls/branches/comparisons, locals/vars/flags,
trainer flags,RNG,bag/item commands,party count,money,text buffers/color and host
requests. Source quirks retained:20th stack push refused but call still jumps;
addvar immediate vs subvar VarGet;u8 script item counts. Core adds original
money cap/overflow handling and HealPlayerParty HP/status/PP-bonus restoration.
src/field_screen.rs adapts NPC/sign interactions, original text/item buffers,
yes/no,script locks and muted presentation. Settings0 field_scripts=1/0 default0
is live; unsupported events discard partial core state changes and report that
the event is unavailable. Technical boundary logged. Snapshot fingerprint now
includes core state, so new source flags/party/money changes can be persisted.
Script execution itself is transient; no mid-event saves. Existing settings/
native save schema preserved. Default/prototype controls remain separate.

Still missing original actor movement/map callbacks/intro/starter scenes,
givemon full party/PC/dex path,general battle/encounters,quest-log and most
specials. Fixed names/instant manually advanced text/static NPCs and acknowledged
muted sound waits are presentation adapters,not original frame/audio parity.
New VM behaviour UNVERIFIED against original C. See README for precise scope.

Final validation: isolated build/package +31 retained library tests pass.
Core startup runs original map-reset script and both clerk branches. Hidden
actual-key --field-smoke passes both branches,exactly1 Potion (57 frames in
final run); user saves/config are bypassed and audio forced silent. Packaged
--check native persistence/corrupt-file checks pass. Fresh Settings0 and clerk
dialogue captures inspected; final readable POKEMON font fallback checked in
field-clerk-font-final.png (original accented source text remains in archive).
Installed C++ executable/config match prior preservation hashes. No new detailed
original-C parity comparisons or broad existing replay suite rerun. Ben's Rust
save format/config remain compatible; the installed game remains untouched.

### Native Rust whole-game port: first foundation batch (2026-10-08)

Ben authorized proceeding with the whole original-game source port after the
handoff discussion. Codex only; no Claude/subagents. New behaviour unverified;
defer detailed original-C comparisons, retain compilation/basic integration.

`rust-prototype/src/core.rs`: source-backed RNG, Pokémon creation/initial moves/
stats/growth, party, bag rules/capacities and TM/Berry containers, flags/vars,
partial new-game foundation and versioned native JSON save/load. Importer:
`tools/import_core.py`; data/core.json + core-provenance.json contain 411 species
entries (386 ordinary +25 legacy Unown),356 moves including NONE,375 positional
items including reserved entries, complete learnsets/natures/six growth tables.
Original table expressions/symbols evaluated by an authoring-only C compiler.
Importing data does not implement all move/item effects. Guest .sav format and
full BoxPokemon fields/encryption, quest-log playback and PC transfer deferred.

`src/persistence.rs` connects core bag/events to the existing exploration fixture.
Settings9 native_state=1/0 (default0), restart required; own output/native-state.json
stores location/facing/running-shoes fixture, Potion/flag and core state. Stable
movement/dialogue boundaries autosave; free movement resumes at logical cell.
Invalid save preserved, writing disabled, HUD reports error. QA bypasses saves.
Rival battle/rewards remain isolated; world party is not yet created by scripts.
Full NewGameInitData/story VM/starter scene/encounters/general battle remain next.
See rust-prototype/README.md for exact scope and limitations. Installed C++ game,
config/saves and pending C++ patches are preserved; Rust packaging stays separate.

Validation: full isolated Rust build/package and packaged --check passed;31
existing library tests passed. New core startup/integration creates every411
species entry at levels1/5/50/100 and exercises party/bag/events/save overwrite/
reload. Packaged --check verifies exploration save/reload and corrupt-save
preservation. Fresh core-foundation-settings.png inspected; Settings9 fits.
Installed executable/config match the previous preservation baseline hashes.
No new original-C parity runs, broad native replay suite or audible QA performed.

### Separate native Rust prototype (2026-10-08)

`rust-prototype/` now contains a source-backed native Rust exploration slice,
developed entirely by Codex. Output: `rust-prototype/output/FireRed Native 3D Prototype.exe`.
Pallet Town, Route 1 and four interiors import pinned pret/pokefirered data
(revision 037335f4c725d7c9aecdac87066f2002b4bd7e14). Importer verified all 1,990
map cells and behavior attributes against the local rev0 ROM; provenance packaged.
Renderer-independent Rust ports: movement timing/turning/running, raw collision,
elevations, ledges, connections, doors/stairs, selected dialogue and one-time Potion.
24 headless tests pass. Default third-person controls now use continuous free
movement: floating point position/heading, normalized camera-relative WASD,
short swept circular collision, wall sliding and no tile turn-in-place delay.
Renderer-independent controller: rust-prototype/src/free.rs. Imported solid
cells/elevations/directional barriers/NPCs remain authoritative. No mesh/vertical
terrain physics yet. Walking/running retain 16/8-tick-per-cell source rates.
Event adapters: aligned door thresholds, proximity stairs/exits, warp re-entry
guard, fractional map crossing, facing nearby NPC/signs with line-of-sight check,
and automatic source-style ledge jumps. Warps/jump endings use original cells.
Own config.ini stores free_movement, third_person, diagonal_movement,
camera_rotation and smooth_movement. F1 then 5 toggles free movement; F1 then 3
restores classic controls; F1 then 4 changes the optional diagonal grid mode.
Esc releases cursor; click captures. Free mode takes priority over grid diagonals.
Native free replay passed arbitrary camera heading/fractional motion plus 131
route steps, four conversations, house floors, Route 1 and one Potion (2,357 ticks).
Existing 130-step cardinal and 133-step diagonal replays are retained. Fresh
free-controller/Settings captures checked. See README for precise limitations.
Original-art pass shipped: 12 baked source textures on outdoor ground/water/
flowers, indoor floors and all sides of house/lab walls, roofs, windows and doors.
Tiles/palettes/metatile definitions checked against local ROM; art-provenance.json
records hashes. Red remains unanimated solid blocks with his actual sprite palette;
trees remain solid abstractions with source greenery colours. No billboards.
F1 then 6 toggles source_textures (default1). F1 then 7 toggles music (default0).
Two offline WAV loops: Pallet ID300 (43.875s), Route1 ID291 (25.25s), rendered from
local ROM with pinned gba-audio-tools 45e84ba8a5a47b22e56dac7e4e87b1ac605bcb19.
Windows native playback; no runtime music interpreter. Interiors reuse Pallet.
Music stops on focus loss; all captures/replays force silence. WAV headers, source
song pointers and non-silent PCM validated; audible playback not auditioned.
24 tests, all native replays, packaged asset validation pass. Fresh views checked;
final fixes remove flat chair artwork from floors and blue lab roof gables.
Final front/rear/indoor and source-textures-off captures checked. Import scripts:
tools/import_art.py, tools/import_music.py; music reference fetch script included.
No zoom controls. No guest runtime or ROM needed to play; importer alone reads ROM.
Installed EXE/config hashes unchanged; installed saves and pending C++ work untouched.
See rust-prototype/README.md for exact limitations, build/import/check instructions.

Opening rival battle added (2026-10-08), still separate native Rust application.
B opens isolated Charmander vs original level-five rival Squirtle; 1 Scratch,
2 Growl, 3 Run (blocked for trainer), R restart, Esc return. Mouse buttons work.
Tab opens Settings without function keys. Settings 8 / battle_slice config.ini
toggle enables the module (default1); Settings 7 enables optional music.
Original source species/base stats/learnsets/move data, rival trainer IVs and
encoded-name personality imported. Player starter is explicit Hardy/31-IV/0-EV
fixture, not a random/save-derived starter. Original trainer AI script data is
interpreted in Rust with original three passes/scoring/random/tie selection.
src/battle.rs: renderer-independent damage, crit, accuracy, PP, lowered stats,
opening tutorial flags, HP/faint/win/loss and 70 EXP / Defense EV / level-six gain.
Original ACC_CURR_MOVE accuracy quirk preserved; damage/stat tutorial flags remain.
src/battle_screen.rs: native UI with original ROM-extracted battle sprites;
exploration freezes and resumes independently. Battle state/rewards are isolated.
No lab sequence, world party/inventory, items/switching/catching, money/story rewards,
general effects/abilities/weather/status or battle save/load. Source AI commands
outside the supported fixture fail explicitly; no fabricated fallback/Struggle.
31 library tests pass. Original-C host oracle copies actual AI handlers, selection
and processing, physical damage branch, accuracy/crit, CalculateMonStats and nature.
980 AI + 13,454 damage + 2,048 accuracy/crit + 400 stat vectors match Rust; AI tests
compare RNG state/call counts too. This verifies bounded command behaviour, not
whole-ROM/frame timing parity (original per-frame background RNG is not reproduced).
Authoring tools: import_battle.py, import_battle_art.py, check_battle_source.py.
Native check-battle.ps1 checks original/diagonal/free replay + battle win/return/
restart through actual keys; fresh battle/settings views are captured silently.
Battle music/animations absent; exploration music pauses in battle. All QA silent.
Final package validated. All four native replays passed; win/loss/settings captures
checked. Final UI fixes hide exploration HUD behind battle and keep text shaping
stable between turns. Both original sprite output hashes verified; installed game
and config still match baseline hashes. Final screenshots: captures/battle-final-
battle-win.png and battle-final-battle-loss.png. Existing exploration tests retained.

- Game folder: `C:\Users\benpu\Desktop\FireRedRecomp-esc-menu`
  (exe renamed by Ben to `Pokemon Fire Red.exe`; Desktop shortcut points at it).
- ROM: `C:\Users\benpu\Desktop\Pokemon - FireRed Version (USA).gba` (rev0,
  SHA-1 41cb23d8…). A copy for the build lives in
  `FireRedLeafGreenRecomp/variants/firered/roms/firered_usa.gba`.
- Settings file: `config.ini` next to the exe (`[Launcher]` + `[Remaster]`).
- `install_files\old\` in the game folder keeps the previous exe.

## Features (all in `FireRedLeafGreenRecomp/src/`)

| Feature | Where | Notes |
|---|---|---|
| Boots straight to the game, window/borderless/fullscreen, 16:9 | `main.cpp`, `fr_widescreen.cpp` | widescreen draws extra map columns from the map grid and verifies them against VRAM every frame |
| Settings inside the game's OPTION screen (Game / System tabs, Interface, Controls, Extras pages), first-run setup screen | `fr_settings_menu.cpp`, drawn by `fr_hd_screens.cpp` | rows = `enum class Row`; add a row in the item list, `row_value`, the change handler and the HD row builder |
| PC controls: keyboard, mouse (hover/click on HD UI, wheel scroll), controller layout + left stick | `fr_pc_input.cpp` | mouse clicks become verified key macros; config load/save is here too |
| Modern HD interface, switchable per part (battles, field menus, dialogue, title menu, Pokédex, screens) | `fr_hd_*.cpp` | ImGui overlay on top of the game; the PPU hides the replaced layers (`g_hd_bg_mask`, mask rects, `g_hd_obj_hide`) |
| Fast forward 2×–10×, music muted, bindable key, hold or toggle | `fr_pc_input.cpp` (`fast_forward_policy`) + engine `runtime.cpp` | badge drawn in `fr_hd_ui.cpp` |
| Infinite Rare Candy (Bag keeps 99) | `fr_pc_input.cpp` (`rare_candy_frame`) | writes the Items pocket |
| 3D "diorama" view (test), F3 toggles | `fr_diorama.cpp` | see below |
| Exe name "FireRed Remaster", icon (Ben's Charizard) | `CMakeLists.txt`, `res/firered_remaster.rc/.ico` | old flame icon kept as `res/firered_remaster_flame.ico` |

### config.ini `[Remaster]` keys

`auto_run, mouse, hd_battle, hd_text, hd_dialogue, hd_title, hd_dex,
hd_screens, modern_text, pad_layout, pad_stick, setup_done, ff_speed,
ff_key (SDL scancode), ff_toggle, rare_candy, diorama`
(legacy `hd_interface` sets every HD part).

### Engine changes (`FireRedLeafGreenRecomp/gbarecomp`, see `git diff` there)

- `src/gba/gba_ppu.*`: HD mask rectangles (`g_hd_mask_rects`) checked per pixel.
- `src/runtime/host_window.*`: controller A/B swap + left-stick D-pad.
- `src/runtime/runtime.*`: `RunOptions::fast_forward_policy` (multiplier,
  mutes audio, presents every Nth frame).
- `src/runtime/mod_function_hooks.cpp`: hook limit raised 32 → 64 (the
  menu/input probes need more).

## 3D view (`src/fr_diorama.cpp`)

Rebuilt in update 17 (Ben: the pixel-guessing approach kept producing glitches).

- **Renderer**: software rasterizer with a depth buffer (`Raster`), triangles
  queued and drawn in horizontal bands on up to 8 threads, perspective-correct
  UVs, alpha-0 texels discarded, blended triangles (shadows, semi-transparent
  sprites) test but do not write depth. Output = one streaming SDL texture drawn
  under the HD UI. ~2.5–3.5 ms/frame at 852x480 (`FRLG_3D_TIMING=1`). The file
  compiles with `-O2` (CMakeLists). Framebuffer scale = min(`FRLG_3D_SCALE` (3),
  drawable px per view unit).
- **Tile model tables** (`kGeneralModels`, `kPalletModels`, `kBuildingModels`,
  `kGenericBuilding1Models`, keyed by tileset symbol addresses): every metatile is
  kFlat (default), kStand (upright object; vertical runs merge unless `b = 1`),
  kTree (pine part: `a` = half, `b` = tip/upper/lower row; each tree is drawn as one
  32x48 model composed from 0x00E/0x00F, 0x01E/0x01F (top layers) and 0x024/0x025
  (keyed), so dense forests become individual trees), kGrass (blades card), kRoof/kWall
  (box: walls upright, roof leans 0.75 outdoors, flat indoors; textured sides).
  `ground` = metatile drawn flat underneath. Plain-ground id lists per table give the
  colours keyed out of art without a top layer.
- Unknown tilesets: everything flat except the door fallback (connected solid
  block containing a door, 3–12 x 3–10 cells = box; walkable top-layer roof edges
  above it join the roof). Indoors the top two room rows are the back wall; side
  walls reuse the commonest wall tiles; floors have a 10-px front edge; pure black
  is transparent (the void).
- Tables done: General (trees, signs, flowers, grass, ledges, white fence),
  Pallet (houses, lab, mailboxes, fences, Route 1 gate trees), Building +
  GenericBuilding1 (player's house furniture). Next: Viridian secondary, PC/Mart
  General tiles, cliffs/mountains, other towns, other interiors.
- `tools/py/tileset_sheet.py` renders tiles straight from the ROM with hex labels:
  `--layout <Map>_Layout ...` (tiles a map uses, with behavior/layer/collision),
  `--map <Map>_Layout X Y W H` (map area in place), `--tilesets A B FIRST COUNT`.
  Use it to fill tables; the layout's tileset pointers are at +16/+20.
- Test-only tuning env vars: `FRLG_3D_ALPHA` (45), `FRLG_3D_FOV` (28),
  `FRLG_3D_ZOOM` (0.76), `FRLG_3D_SCALE` (3), `FRLG_3D_TIMING=1`.

## Testing and debugging

- `tools\run-debug.bat` starts the game with the TCP debug port 19842
  (protocol: `FireRedLeafGreenRecomp/gbarecomp/TCP.md`).
- `python tools\py\tcp.py mem 0x03005008 4`, `u16`, `io`, `raw '{"cmd":"savestate_save","path":"x.state"}'`.
- Player location: `gSaveBlock1Ptr` (0x03005008) → +0 pos x/y (s16), +4 map
  group/num.
- `python tools\py\warp_save.py IN.sav OUT.sav GROUP NUM X Y` makes "Continue"
  start at a map position (Pallet Town = 3 0 10 10).
- Scripted key presses can be dropped; verify results through memory.
- Focused rendering checks: `python tools\py\check_diorama.py C:\msys64\mingw64\bin\c++.exe`
  compiles the real rasterizer, tile tables, art layers and sprite decoder against
  fixtures (depth order, transparent texels, blended shadows, model lookups, roof
  surface invariant, flat furniture tops, sprite flips/affine/hidden).
- `tools/capture-3d.ps1` runs a silent SDL dummy/software renderer against the
  copied ROM/save in `tools/check-game`, so validation opens no desktop window.
  `-Resume -Frame 60` can reuse `check.state1`; captures include the final 3D/HD
  overlay via `GBARECOMP_RENDER_CAPTURE` / `GBARECOMP_RENDER_CAPTURE_FRAME`.
  `-State check.state2 -Save room.sav -Resume`
  reuses the copied house-interior state; defaults still use the outdoor state.
  `check.state3` holds the copied upstairs bedroom. Captures default to scale 4
  (1136x640); `-Scale` changes test resolution without changing game settings.
  `-Replay continue.csv -Save route1.sav` boots a copied save via Continue (saves made
  with `warp_save.py`: route1 = 3 19 11 30, route1s = 3 19 12 37). `toolsuild.bat`
  now copies the exe to `tools\check-game\FireRedCheck.exe` itself.
  Update 17 checked Pallet, Route 1 (south gate, west forest), Viridian (fallback
  boxes) and both house floors. Tests use copied saves only.

## Gotchas

- Build only the `FireRedRecomp` target.
- Summary screen move ids are at +0x3258.
- Menu macros hold a key until `gMain.heldKeysRaw` (0x03003118) shows it;
  `read()` returns -9998 = busy (wait), -9999 = cancel.
- List/menu classification is "sticky" for 20 frames (probes only fire on
  input frames).
- In widescreen, windows touching a screen edge are anchored to the view edge,
  so HD masks for them extend to x -160 / 400.

## TODO / ideas

- Mouse polish pass (Ben: "glitchy, deal with it later").
- 3D tables for the remaining tilesets (Viridian first, then PC/Mart, cliffs,
  other towns/routes, interiors).
  Ben allows camera/style changes if they improve the view; no zoom settings.
- HD Pokédex Area/Size page; evolution scene still classic.

## History

1–4: fullscreen, Esc/settings, boot to game, 16:9, Pokémon-style settings,
icon, rename, PC controls, complete save, full HD UI redesign.
5–8: per-part HD toggles, first-run setup, mouse wheel-only scrolling, less
flicker, fast forward, infinite Rare Candy, quantity stepper.
9: Ben's Charizard icon; installer keeps the renamed exe.
10: 3D view test (F3, Extras → "3D View (test)").
11: stable ground/forest geometry, sprite transformations/reflections, live door
frames and map-transition camera reset. MSYS2 compiler installed after bypassing
the redirecting mirror's HTTP 429 errors; cached packages reused.
12: fixed dense-forest folds and exposed brown rectangular strips; validated
silent Pallet Town captures and scrolling using a copied save. Game volume set to
zero for Ben's YouTube use; original save preserved.
18: Claude Code cloud implemented N-S fence panels and extracted the Route 1 gate's
bottom-layer fence stripes from tree tiles; corrected gate tip half tags. Local
renderer checks, Windows build and silent gate captures passed. Cloud-first work
is now recorded in AGENTS.md; routine implementation choices require no questions.
17: 3D view rebuilt: explicit tile model tables, individual tree models, depth-
buffered multi-threaded software rasterizer, door-block fallback, tileset_sheet.py.
16: forest canopy blocks, room side walls/floor edge, N-S fence panels, fixed tree
classification after warps (palette timing), ledges and path-edge tiles.
15: removed dark under-object rectangles, upright forest rows, standing tall
grass, flat N-S fences; checked Pallet and Route 1 captures.
13: closed building sides and shared roof alignment; indoor cutaways with low
furniture, flat rugs and no black-void geometry. Silent captures checked Pallet,
both floors of the player's house, stair and exterior-door transitions. Camera
defaults retained; volume remains zero and the original save is unchanged.

## Private GitHub backup (2026-10-05)

GitHub CLI installed and authenticated. Ben authorized private commits/uploads; the former project publishing restrictions have been removed. Source snapshot uploaded to https://github.com/benpushkarev-commits/FireRedRemaster. See docs/GITHUB.md for exclusions and the separate upload checkout. Desktop changes do not upload automatically.


## Claude cloud workflow (2026-10-05)

Private GitHub checkout now includes cloud-specific Claude instructions, a Linux setup hook, and ROM-free renderer checks. Cloud/Linux instructions are in docs/CLOUD.md. The renderer checker accepts a compiler argument or defaults to CXX/c++. A Claude cloud verification session has been launched using FireRedRemaster/main. Playable game builds and screenshot validation still require this Desktop project and its excluded local inputs.


Cloud verification completed on 2026-10-05: Claude ran cloud-setup.sh and cloud-check.sh in /home/user/FireRedRemaster. Both exited 0; renderer output: 3D rasterizer, tile models, art layers and sprite checks passed. No files changed. Session: https://claude.ai/code/session_01G4piHz97QassBLs9fwC9PH


## Claude cloud-first development (2026-10-05)

Ben explicitly prefers Claude Code CLOUD for implementation to reduce Codex usage,
with routine decisions handled autonomously. Codex reviews focused private branch
patches, builds on Windows, checks new silent captures using copied saves, and
installs the existing Pokemon Fire Red.exe. Cloud coding does not replace local
ROM/BIOS-dependent visual checks. Do not silently substitute the local Claude CLI.

Current reusable session: https://claude.ai/code/session_01PxiancrqqEBQM2rTHBvJwN
Branch: claude/magical-rubin-481tak (private; not merged to main; no PR).
Latest locally verified/deployed commit: 7b123fd; renderer blob: 0c98324d717d33c1bdba53e9fd95329ca7f63c1c.
Cloud and Desktop checks passed; the full Windows target compiled and was deployed.
Cloud changed only src/fr_diorama.cpp and tools/py/check_diorama.py, plus its handoff.
The Desktop checker retained its existing local compiler setup when applying patches.

Gate fence art is on the BOTTOM layer in special tree tiles: 2AB/2BA columns 12..15,
2AA/2B2 columns 0..3. isolate_stripe() removes known floor/backdrop colours, preserves
native fence colours and keeps pixels stable until the depth rasterizer flushes.
Tip half tags now agree with the upper halves. Ordinary fence ids can also form N-S
panels based on neighbouring fence cells. Settings/camera defaults remain unchanged.
New captures cloud-gate-layer-fixed.bmp and cloud-gate-close.bmp verified both gate
panels and their corner joins, intact tree models and ordinary E-W fences. Original
save hash remains 1916179B3B01D536C6912E8ADAE8BC28BF619146269AD5F63174EF454A6E7582;
installed volume remains 0. No ROM/BIOS/save/generated code was uploaded.
Graphify's local command currently fails with a uv trampoline path error; use scoped
rg after the required query attempt rather than spending tokens on unrelated setup.

## Settings navigation and geometry follow-up (Claude cloud, 2026-10-05)

Ben wants sustained autonomous improvements using the included Claude Code cloud
credit allowance (last checked: $98 of $100 left, auto-applied; expires November 5).
Do not buy credits, enable paid usage credits or auto-reload. Continue the reusable
cloud session/private branch above, with Codex doing Windows validation/deployment.

Settings commits 065255a + 2326412 are applied locally and compile on Windows.
HD settings have General / Interface / Extras / Controls tabs; OPTION page 2 also
has Game. L/R follows the displayed category order; B/right-click goes back one
level; Esc/Guide resumes directly. Categories remember cursor/scroll. Classic
menus retain the cartridge style and now label Extras correctly. No zoom controls.
New ROM-free checker: tools/py/check_settings_nav.py (optional compiler argument).
Fresh silent checks: HD Extras, scrolled Controls, classic Extras; live standalone
Extras -> Controls -> Back -> Esc resumes; native OPTION page2 -> Extras -> Game,
reopen page2 -> Extras -> Esc closes. No original saves or installed settings touched.
Diagnostic env vars (off unless set): FRLG_SETTINGS_OPEN=standalone:extras,
FRLG_SETTINGS_SEQ="open=standalone;cat=extras;step=1;back;close", FRLG_SETTINGS_GAP
(default 20 pumps). Sequence logs go to each capture's .err. Replay must wait until
PPU frame2700 after check.state1 before Start. That copied save has no Pokedex:
OPTION is four Down presses from Pokemon. Native-Game diagnostic step is currently
a no-op because menu_input returns when mode=None; cloud correction is requested.

Geometry commit 587601e is applied locally; renderer fixtures and Windows build pass.
Building/furniture sides sample native art columns per row and have contact shadows.
Fresh house/bedroom/Pallet captures show reduced dark side slabs, but upstairs TV
parts remain split/floating and doorway lip has a detached brown strip. Not deployed
pending a focused cloud correction with measured native part ids/layout coordinates.
1F plants: 057/05F left and047/04F right; table04C/04D top,054/055 front.
2F TV column x6: y3=286,y4=035,y5=28E,y6=296; bed x1..3,y4..6:
283/284/285,28B/28C/28D,293/294/295. 1F door threshold x3..5,y8..9:
012/013/014 then01A/01B/01C; other bottom-row cells008 are black void.
Pallet 2C0..2C5 are lab WALL tiles; roof is2B0/2B1/2B3/2B4 and2B8/2B9/2BB/2BC.
Native art stayed local; only numeric model/layout metadata went to Claude cloud.

### Verified and installed: settings + house geometry (2026-10-05)

Cloud commits through 7b123fd are now built and deployed as Pokemon Fire Red.exe.
Renderer and settings navigation fixtures pass; Windows FireRedRecomp target passes.
Fresh captures cloud-tv-rug-fixed.bmp and cloud-tv-kitchen-kept.bmp confirm continuous
rug under the TV, intact downstairs kitchen/table/plants, a low bed, and the clean
entrance lip. cloud-geometry-gate-kept.bmp verifies the Route1 gate still has its panels.
Native live full-ring test: page2 -> Extras -> Game -> L -> Controls -> Interface -> Esc.
Only known Building exit-mat ids01A/01B/01C are excluded from the room shell; irregular
room floors are retained. TV286 has a top layer (rows13..15): the previous no-top
inference was wrong. The patch came from035's hidden wall BACKGROUND being drawn as
floor. Explicit GenericBuilding1 TV-run context now selects known adjacent rug tiles
(044 border /045 middle) underneath lifted parts;035 elsewhere is unchanged.
Installed volume is0; build/install hashes match; original save hash remains unchanged.
Latest renderer blob is recorded by the hash-object verification in the task log.
Remaining work includes fuller town/tileset model coverage and 3D clarity. Goal remains
active; cloud credit last checked at$96/$100; no extra usage credit/auto-reload enabled.

### Verified and installed: Viridian town batch (Claude cloud, 2026-10-05)

Installed through cloud59cd140. Exe build/install hashes match; volume remains0,
original save hash unchanged. Fresh silent Center/Pallet/house1F captures checked.
Exact Viridian/General models now include the Gym, both green houses, Mart and
full Center. Shared roof planes join properly; recessed walls have their own
anchors, lintels and segmented sides. Known grass below exterior boxes avoids
native combined roof artwork. Explicit cap art uses top layer only, including
the side-height scan. Native flowerbed art stays on front faces.

Two important native findings: House cap280..284 was omitted and painted flat;
Center187/18F are rightroof,197/186/19F are rightfacade, NOT tree overlap. Models
are scoped to General+Viridian. Fixtures exercise real connected components,
roof vertices/caps, door geometry, art selection and raster/sprite behavior.

BG0 was ruled out as a town defect source (zero nonzero pixels, hide proof left
artifacts). Original default overlay retained; window gating diagnostic only.
Diagnostic env vars (off normally): FRLG_BG0_HIDE/DIAG/GATE; FRLG_3D_LAYERS hex
bits1ground,2trees,4stand,8boxes,10shell,20sprites; FRLG_GROUND_MODE bottom/top/keyed;
FRLG_BOX_DIAG=1 or world-cell rect; FRLG_ART_DIAG comma-separated metatile IDs.
Numeric diagnostics use WORLD = native layout+7. House2world31..35,22..26;
Centerworld31..35,29..33. Do not interpret layout coords as diagnostic coords.

Native fast states: check.state1 Pallet/check.sav;state2 house1F/room.sav;
state3 bedroom/room.sav;state4 towncrossroads/viridian.sav;
state5 nearGym/viridian-gym.sav. Resume capture160frames; higher mix320 if needed.
State4/5 created via copied saves plus GBARECOMP_ASSIST_SCRIPT=2050:save4/save5.
Capture tools/capture-3d.ps1 uses hidden process, dummy SDL audio/video andvolume0.
Never browse old captures: only open new targeted validation for modified code.

Next cloud batch active: audit and repair settings input/usability/persistence,
keeping original styling, no zoom, no routine Ben questions. Installed town
batch stays in place while further candidates are tested. Included Claude cloud
allowance last checked$92 of$100; purchased credits/auto reload remain disabled.

### Verified and installed: character clarity + settings/input (through b288280)

Installed exe: C:/Users/benpu/Desktop/FireRedRecomp-esc-menu/Pokemon Fire Red.exe.
Build/install hashes match, volume0 and linear_filter0 preserved, save unchanged.
3D frame matches output pixels up to1.7M budget; existing Smoothing governs any
remaining scaling. <=4% camera pixel alignment, no zoom settings. Transparent
quad siblings blend once, removing double-blend diagonal seams. Fresh4x/6x/7x
captures checked: character sharper, town geometry intact. Software 3D drawing
CPU5.80/12.42/11.09ms perframe (render cost only; parallel dummy-driver tests).

Settings action rows have full-width targets; labels select Volume without
changing it. Launcher config changes save immediately with comment/unknownkey
preserving merge. Live mouse hover/category changes disarm Quit; key captures
block all input routes, rightclick/PadB/Start cancel. Real SDL event-loop tests
verified F11/F5/Enter/F3 events swallowed during FF capture, PadB/Start cancel,
mouse cancel and closing clear raw capture. Actual hardware-key polling is not
proved by pushed events. Volume10 persisted while menu stayed open; test config
restored. HDControls/classicExtras visual checks and nav/renderer fixtures pass.
No build-stamp clutter in game UI; startup stderr identifies compiled build.

Next cloud batch active: reverse GBA-binding conflicts with reserved F-keys and
current FF key, clear refusal feedback, action/binding help text, classic title
contrast. Installed stable b288280 remains until that batch passes native tests.

### Verified and installed: room joins + control feedback (through 672bc0a)

Native Windows build and renderer/navigation fixtures pass. Center1F corner voids
now take plain wall pixels, keeping authentic opaque art; top edge is continuous.
Side walls use a repeated plain tile or a flat body row from the upper wall,
instead of door, plant or equipment art. Fresh Center/house1F/bedroom/town captures
checked. Room bounds diagnostic confirms Center world x7..21, back rows7..8;
shell planes112/352, floor rows9..15. FRLG_ROOM_DIAG is off by default.

GBA key capture refuses F1-F12/current FF key and stays pending with visible reason;
Z binds, Esc cancels, refusal resets. Real SDL tests passed. Confirm is Controls
row2 (rows0/1 are Controller/Left Stick). HD binding/action help and classic category
contrast/capture hints checked on fresh captures. Classic FF hint covers its own
capture too. FireRed uses controls.ini, NOT generic keybinds.ini; back up/restore
controls.ini for mutating tests. Test controls restored from installed original.

Deployed Pokemon Fire Red.exe build/install SHA256:
80623BDF2D16BC9FA666D16A155DFED0CE9919ADC532970B8454620F175BB876.
Installed volume0/linear_filter0 and original save hash unchanged. Included Claude
cloud allowance last checked $81 of $100; purchased credits and auto reload off.
Next cloud batch active: existing FF/Space binding precedence, flat wall empty-row
edge case, capture lifecycle audit. Stable 672bc0a stays installed until checks pass.

### Verified and installed: low seats/table + input robustness (through 4ec491f)

Installed Pokemon Fire Red.exe includes d2cc1f1,50fce24,c448bae,c0c8e69,4ec491f.
Center table is one raised box with native top artwork, base/sides and floor
underneath. Stools are independent horizontal seats at z4 with short front/side
faces, rather than upright cards. Native top-layer diagnostics confirmed all six
furniture IDs have_top=1. Fresh low-seat capture checked; previous house regression
and scoped model/component fixtures pass. Counter/stairs stay authentic flat art.
Actual table world bounds18..19,13..14; earlier approximate native x was one off.

Existing FF/game-key conflict now gives game input precedence and Settings explains
it (classic SPACE (OFF), HD readable help). User bindings preserved. Already-latched
FF is suppressed during naming/capture/conflict, then resumes after the block;
state regressions exercise this and hold/toggle cycles. Whole Windows build passes.
Real SDL events: PadB clears GBA capture AND refusal; PadStart clears FF/raw capture.
Mutating test controls.ini/config.ini restored. Pushed events do not prove SDL
hardware-key polling; FF state coverage is through tested helper plus native build.
Flat wall sampling skips transparent rows, regression fails against old code.

Build/install SHA256 E23921627597E934177EE0C382B419D17E8B0093C470F0DEA133EC34F632FE52.
Volume0/linear_filter0 and real save preserved; build/install hashes match.
Next useful work: native counter top/front split and nurse depth, other interior
furniture and visual/input audit. Claude cloud remains the implementation route.

Latest budget check: Claude included cloud credit $78 of $100 remaining. Next cloud
batch active: evidence-first Center counter/healing-machine depth and helper
bounds/null audit. Verified 4ec491f remains installed during that work.

### Verified and installed: counter strip + safe wall sampling (through 88a3789)

1440a48 bounds/null fixes and per-row wall material memoization pass Windows
renderer fixtures/full build; cloud checks additionally use ASan/UBSan on Linux.
COUNTER_DIAG native idle nurse OAM4 foot_world232,160 cell14,9, behind front row10.
2BC/298 now shallow6px blocks, top art0..9/front10..15, explicit floor281 because
2BC bottom layer is empty. Shared internal sides hidden; end sides only; no stacked
per-cell shadows. Native default45 and test tilt35/55 captures checked: nurse
visible, counter depth/edges good, tables/seats/flowers intact. Test angles only;
installed camera remains default. Mixed corners and machine row await masks.

Deployed usual Pokemon Fire Red.exe; build/install SHA256 matches:
22B5A30383CE1DBA33186C0EED9A21363981D7F0FB81657DB9F842F0B3CD297B.
Sound0, smoothing0, original save hash unchanged. Both Desktop Pokemon shortcuts
point to this exe. Included cloud credit last checked$76/$100; paid usage off.
Cloud task ACTIVE: mixed corner flower/counter layer masks and healing-machine
geometry. Sent native corner top+bottom/machine row stats from
 tools/claude-counter-layers.txt; request more specific numeric diagnostics if
needed (collect locally, no ROM/image upload, no Ben questions). Claude session
session_01PxiancrqqEBQM2rTHBvJwN, branch claude/magical-rubin-481tak; current source
context~641k/1M. Use paste() for long prompts; typeText() timed out and left a
partial draft, corrected before send. Browser proof tools/claude-cloud-progress.png.

### Resumed batch: counter corners and flower pots (7a79ad8)

Applied Claude cloud 7a79ad8; full Windows build and renderer fixtures pass.
Counter corners 2B9/2BD now use their opaque bottom-layer counter artwork as
6px blocks with floor281 beneath. Top-layer rows0..4 form upright flower pots;
repeated counter rows8..15 are excluded. Nurse cells290/363 retain their bottom
floor and raise only top rows12..15 into a continuous ledge. Native default45
and test35 captures checked: nurse visible, continuous counter, proper end caps,
flowers/table/stools/walls intact. Installed usual Pokemon Fire Red.exe.

Expanded COUNTER_DIAG rows collected locally in tools/claude-counter-resumed.txt
and relevant numeric rows sent to Claude cloud for machine/back-row geometry.
No ROM or artwork upload. Included credit $76/$100 remaining; task running on
same private branch/session, context666k/1M. No purchased usage changes.
Build/install SHA256 verified equal: 6138ECB4A7BFBC0EBDC5C64E8BC280E3DB37B25638503BDCE52912BD3BBC5EC7. Original save hash unchanged.

Native review rejected machine batch 7c93b4a/be63cbe: applied to source and
Windows build/fixtures passed, but machine lid z12 projects across upper art,
stretching green lights into bars; terminal becomes a white slab. NOT installed.
Installed exe remains 7a79ad8 (6138ECB4... verified above). Claude correcting
coherent full28-row machine body with genuine wall retained, plus possible
combined15-row flower masks. Current build/check exe contains the rejected
machine geometry until correction; do not deploy it without fresh visual check.
49b80eb corrected full28-row machine and combined15-row flower geometry verified in fresh default45/test55 native captures; fixtures/full build pass. Installed exe SHA256 85E3CECEA0FD6D5F2F5390FB4DF9B0133100054872C2441710B85613406A4319. Rejected lid removed, terminal flat awaiting measured split, save unchanged, audio0/smoothing0.
377a881 terminal20-row shared-plane geometry + HD help overflow fix verified and installed. Native45/55: housing/red cross/blue base intact, no stretched cap or upper-wall overlap. Standard240-width Smoothing and FF key-conflict help fit inside panel. Renderer and settings fixtures/full Windows build pass. Installed SHA256 8D98C3DB430886C232871C66F8EA37E180191C4E701E9ACFEC4E2ECBFDADC06B. Original save unchanged. Local capture helper now accepts -ViewWidth (default284) for standard240-width UI verification; game settings unchanged.
17b8fc3 rendering stamp-buffer optimization + measured help truncation verified and installed. Meaningful stamp/frame/resize/wrap and proportional text-layout fixtures/full native build pass. Fresh scale7 Center and240-width FF-conflict help checked: no visual regression, geometry/art remain intact, text inside panel. SHA256 FBB0C50D44AEE1744447C4DFD302E912D5DE91EC8455ACA2055A730B0D04546A. Save unchanged; audio0/smoothing0. FRLG_SPRITE_DIAG collected in tools/claude-sprite-resumed.txt; measured framebuffer pixels per art pixel are player x4.738/y5.036, NPC x4.246..4.738/y4.957..5.036, nurse x3.971/y4.865 at rs6.117. These are not all integer values despite crisp nearest edges. No sprite/camera style change made.
de24a49 sprite screen-position phase snap verified/installed. Translation only, maximum0.5 framebuffer pixel; size/camera/art/depth unchanged. Renderer fixture proves stable texel column-width pattern over10subpixel offsets, retains width; native fullbuild + Scale7 walk/approach counter replay tested with crisp actor and proper scene occlusion. Native captures validate appearance/occlusion; reduction of phase swimming is quantified in fixture, not a video comparison. Final installed SHA256 DA402F24CE6FAE51D1F84ABAFB0A38F046C64C72B616179C905536EE59D35E48. Existing sound0/smoothing0/widescreen1 retained, original real save hash unchanged.

Latest visual-priority checkpoint (2026-10-05): source/build/install through
49da788 (house batch below, then b96be1f/46b85aa/a981793/49da788 applied locally).
The uncorrected b96be1f forest build FAILED native visuals and was never deployed.
Claude's
0b33063 broader UI audit was documentation only; Ben redirected full priority to
persistent 3D visual defects. Camera controls are deferred; no zoom settings.

Mixed-surface furniture now splits at its actual top/front boundary instead of
drawing a sloping quad through both planes. Dining table 054/055: top rows 0..6,
front rows 7..15. Bed 293..295: horizontal blanket/fold rows 0..10, wood front
11..13, baked shadow 14..15 omitted in favour of the renderer's contact shadow.
Bed bottom-layer floor quarters preserve the original wood; object quarters are
replaced using floor tile entries, avoiding duplicate pillow/frame or rug pixels
borrowed from adjacent furniture. Console 296 is separate and grounded at its
lowest opaque art row. Native ART_ROWS data measured these boundaries.

Viridian house 2: the wall is one row, door 299 shares its plane; walkable
2A0/2A2..2A4 are low planters with flat tops/fronts/end caps. Wall tiles
298/29A..29C split their lower planter band, and house side walls close down to
the floor. 29B's blue window-box section joins the continuous planter top;
fresh left/right captures show no notch. House 1 is unchanged. Generic standing
and orphan classification guesses remain environment-only, OFF by default.
New diagnostics: FRLG_ART_ROWS with FRLG_ART_DIAG, and FRLG_SCENE_AUDIT.

Full Windows build and renderer/model fixtures pass. Fresh hidden, muted native
captures checked house/bedroom and Viridian from both sides. Route 1, Forest and
Pewter movement audits found no classification flips or origin jumps >4 pixels
during their sampled field frames; Forest walk entered a battle fade, so this
does not verify battle rendering. New copied states: check.state7 Forest
(forest.sav, group1/map0,26,60), check.state9 Route1 (route1.sav).
Installed Pokemon Fire Red.exe SHA256:
A4A0339641190DD9ED39942F53508D670B67FADB42B7C548F57C72813127D496.
Built/installed hashes match; real save hash unchanged. Config: volume0,
linear_filter0, widescreen1. No user game process was running at deployment.

Claude cloud remains ACTIVE on explicit measured Forest object models in the
same private session/branch. Native Forest sample has 103/240 flat blocking
cells; the round 3x3 tree and repeating boundary canopy use unmodelled secondary
IDs. Entrance roof 044..046/04C..04E is authentically wide/repeated, not corrupt
art. Numeric ART_DIAG/ART_ROWS collected locally; no ROM/artwork uploaded.
The isolated Forest tree correction IS now installed (see below); dense canopy
and gate roof remain flat, with the next canopy batch running in Claude cloud.
Later cloud patches still require local review, build, native captures and deployment. Included credits
last read $67/$100 (not a current balance); paid usage remains disabled.
Background Codex goal remains usageLimited; manual user-requested turns continue.

49da788 Forest isolated tree verified/installed: exact 3x4 pattern includes the
281 tip above the centre column of 288/289/28A, 28E/2A1/28F, 2A3/2A4/2A5.
Wildcards beside the tip retain their own floor and are not claimed. Native
foot diagnostics show 2A3/2A5 contain only the baked oval; they contribute no
upright art. 2A4 retains bark/outline rows0..4 only, with the oval green removed.
The measured 11 trailing empty rows are cropped and the tree is lowered to its
actual trunk foot; contact shadow is horizontal. Snapshot margin expands by4
cells only for the Forest tileset pair, preventing visible partial-object
classification without adding work or changing room inference in other maps.
Fresh Scale6 idle/walk captures show a rounded top, clean silhouette, grounded
trunk and no green poster. Renderer fixtures and full Windows build pass.
Final built/installed SHA256 matches:
F2D20759FB638D2900E161FF78643E6397F409329A6F6DB4AE65AB95734773EA.
Real save hash unchanged; volume0, linear_filter0, widescreen1 retained.
Remaining Forest boundary repeats top rim28A/288/299 then 3x2 blocks
292/290/291 and29A/298/299. It requires its own canopy structure, not isolated
tree matching. Claude cloud is ACTIVE on that explicit next task; no camera/UI
changes, generic/orphan guesses remain off, later patches are not yet installed.
Fresh numerical evidence is in tools/claude-priority-forest-*.txt and native
captures tools/check-game/priority-forest-grounded*.bmp. No artwork uploaded.

78347ae dense Forest canopy trial reviewed locally (NOT deployed): renderer
fixtures and Windows build pass, but fresh native priority-canopy-trial24.bmp
at FRLG_3D_CANOPY=1 / H24 fails visual acceptance. Tall plain green pillars at
keyed seam gaps, comb-like exposed-front holes, and side faces overlap the gate
roof. Trial remains OFF by default; installed EXE stays verified 49da788.
Claude cloud received descriptive native failure feedback plus keyed row stats
for288/28A/299/29A/291/292 and is actively revising the canopy surface/footprint.
No artwork uploaded. Preserve original style, no camera/UI/zoom changes.
028368a height-field revision also NOT deployed/default-enabled: native H16/R2
removes pillars and improves path/gate-roof slopes, but the forest's terminal
front remains split (raised290/292 beside flat2A1, trunk/shadow still flat).
Fresh idle/walk images priority-canopy-slopes16.bmp / slopes-walk.bmp inspected;
no class flips/origin jumps in sampled field walk. Original numeric map grid
confirms worldy60 terminal290/2A1/292 over y61 2A3/2A4/2A5, with side2A2.
Claude is correcting exact terminal-front/side ownership and joining, preserving
isolated tree ownership and flat baked shadows. Installed checkpoint remains
49da788/F2D20759... until native acceptance. Numeric evidence only transmitted.

2f4dd52 dense Forest canopy accepted and DEPLOYED. Exact body/rim, terminal
290/2A1/292 (side2A2) and trunk2A3/2A4/2A5 are modelled coherently; shared
height-field vertices form textured slopes with ground-level path/roof edges.
No tall colour pillars/comb walls/detached lower-leaf chunks. Bark stands on
measured foot; baked oval remains flat; isolated3x4 tree ownership preserved.
Default H16/R2 inside existing 3D toggle, Forest pair only; test ramp clamped1..3
within4-cell margin, FRLG_3D_CANOPY=0 is explicit diagnostic fallback.
Windows renderer fixtures and full build pass. Fresh native idle and walking
captures inspected; default-enabled capture SHA256 equals accepted c48 trial
byte-for-byte. No sampled class flips/origin jumps. No camera/zoom/UI changes.
Built/installed Pokemon Fire Red.exe SHA256 matches:
0B46AE34E8D3F213CDDE343AD6E81C7DB9F925CFEA19D4F76DE05A8918238476.
Real save unchanged (1916179B...); config volume0,linear_filter0,widescreen1.
No user game was running at deployment. Prior rejected trials never installed.
Claude cloud ACTIVE next on Forest gate roof, with measured grid/row data.
Correct roof width: eleven columns x31..41, worldy70..75 ends at map bottom;
entry/warp cells world35/36/37,y69, behavior065. No southern facade in this map.
Gate roof still flat in installed build. New roof trial must stay gated until
native acceptance. No artwork/ROM upload. docs/STATE cloud hunks excluded.

f6bd830 Forest gate roof accepted and DEPLOYED (2026-10-06). Native e359f68
H16/R2 idle/approach captures inspected: continuous hipped roof with original
art, scoped Forest11x6 footprint, textured slopes to canopy/path boundaries.
Walked right32/down48 from copied Forest state through actual south gate warp;
settled Route2_Entrance interior (layout08320990) rendered with no scope leak.
Default-enabled roof capture equals accepted trial byte-for-byte. Renderer
fixtures/full Windows build pass. Built/installed Pokemon Fire Red.exe SHA256:
40EF4A99E9189F5CD8F18F5F04598ACDCC502A8C40421388685B809CED5019EF.
Real save hash1916179B... unchanged; volume0,linear_filter0,widescreen1 retained.
No user game running at deployment. FRLG_3D_GATE_ROOF=0 diagnostic flat fallback;
existing3D toggle still controls all geometry. No camera/zoom/UI changes.
Claude cloud ACTIVE on Route2_Entrance/GenericBuilding2 plant/table/chair and
sidewall-material correction. Fresh numeric map/headers/TOP row data queued.
IMPORTANT: ART_DIAG is one-shot, so reading room IDs during Forest->gate warp
produced WRONG Forest-palette art; discarded. Correct data is captured fresh
from copied check.state10 (gate interior, native PPU~2300); new files
claude-priority-gate-room-{headers,grid,plants-top,furniture-top}.txt.
Resume assist scripts use HOST PUMP count (245:save10), not absolute PPU frame
(2300:save10 never executed). Slots1..10 supported in diagnostic script.
Next interior patch is NOT built/installed yet; exact scope Building082D4BB4 +
GenericBuilding2 secondary082D4E84, room15x12. No artwork uploaded.

8b97615 GenericBuilding2 room correction DEPLOYED. Pair-scoped explicit plants
2EA/2E9/2E8+2F1/2F0 and independent chairs2E6/2E7 use TOP foreground only;
authored bottom wall/floor stays separate. Bare wall selection excludes top
layer decorations for this pair: lower28F/upper287, removing plant art repeated
on side walls. Table2D6/2D7 top +2DE/2DF cloth0..9 horizontal, front10..14
stands5units, empty15 skipped; TOP art preserves outline grey (keying would
remove it). Diagnostic FRLG_3D_GATE_ROOM=0 bypasses models. No other pairs change.
Renderer fixtures/full Windows build pass. Fresh native gate room idle,
movement and approach behind table inspected: clean side walls, grounded
independent pots, horizontal table/front, separate chairs, original floor and
wall intact; no sampled class flips/origin jumps. check.state10 PPU2298.
Built/installed Pokemon Fire Red.exe SHA256 matches:
86E0865DF292DA9D96F3A1FACEB2D3321E3FAAE887822E9E81A164AEF9769580.
Real save unchanged1916179B...; volume0,linear_filter0,widescreen1 retained.
Claude cloud ACTIVE on systematic shared rasterizer/model geometry audit for
proved holes/texture distortion/shimmer/depth/edge/grouping defects. Synthetic
fixtures must show an actual issue before a change; preserve original style,
no camera/zoom/UI changes. Next audit patches not built/installed yet.

ac7101a shared rasterizer transparency fix DEPLOYED. Proved fixture: a near
blended triangle submitted before a far opaque one was overwritten. Flush now
renders opaque first, then blended, preserving submission order within each
pass and disjoint thread row bands. Windows fixtures/full build pass; fresh
Forest, GenericBuilding2 gate room, and Viridian town native captures inspected
with geometry/style intact. BMP hashes differ (blend/shadow effect), NOT
byte-identical; no native geometry regression seen. Cloud ASan/UBSan audit
found no shared ground-cell cracks across300 scratch camera phases; cheap24
phase/decals/depth and fuzz checks retained, not evidence every game map works.
Built/installed Pokemon Fire Red.exe SHA256:
A8F19D39CBE934D36FCA32BC05D59D11050AFE5DFE03624BB6DE09B0A3F9768B.
Save unchanged1916179B...; volume0,linear_filter0,widescreen1 retained.
Claude cloud ACTIVE next on TEST-ONLY character sprite projection experiment
for original pixel-art clarity (nearest width variation/perspective trapezoid).
Requires real-camera synthetic proof, original foot/depth/occlusion preservation
and native acceptance before enablement. No camera/zoom/style settings, no new
artwork or uploads. Current EXE retains existing character projection.

2c863d9 character rectangular projection TEST-ONLY, source/fixtures reviewed,
Windows build passes; fresh Viridian idle and four-direction walking captures
inspected (priority-character-rect-*): original sprite art/proportions clearer,
grounded feet, no ghosts/clipping. Not yet installed/default-enabled. Claude
is restricting rectification to active guest ObjectEvent actor sprite IDs
(exclude non-actor animated-door/effect OAM), and extending stripe fixtures to
check internal column positions across the diagonal before enablement. Existing
reciprocal-depth UV on a screen rectangle may bend interior columns; only fix
if reproduced, keep true row/depth/foot mapping. No integer resizing/zoom.
Installed EXE remains ac7101a/A8F19D39... during this work.

d272ae8 character clarity correction DEPLOYED (2026-10-06). Active visible
ObjectEvent actors matched to guest sprite/OAM shape, size and tile signature;
rectangular projection excludes non-actor effects/doors. Affine horizontal u
for rectified quads removes proved internal column bending; perspective v and
true depth/foot mapping retained. Synthetic stripe/row/occlusion/actor fixtures
and full Windows build pass. Fresh native Viridian default idle and all four
walking directions inspected; sharper original sprite art, grounded feet,
no visible ghosts/clipping. Default idle BMP differs from diagnostic rect=0,
confirming live actor recognition takes effect. Fresh gate-room behind-table
capture preserves furniture overlap, grounded pots/chairs and room walls.
Built/installed Pokemon Fire Red.exe SHA256:
22094B020671B72041F8074722BE537DF0BA76768B8916EBABC29AE12AA6A6FE.
Real save unchanged 1916179B...; volume0, linear_filter0, widescreen1 retained.
Fresh captures: priority-actor-final-{idle,up,down,left,right,table}.bmp.
Claude cloud ACTIVE on READ-ONLY evidence-based audit of blended-over-blended
depth order, animated actor matching and remaining model coverage. Explicitly
no source changes/commits/pushes in this audit. Further fixes are not installed;
local review/build/native checks/deploy require a later coordination pass.
Camera control remains deferred until visual stability; no zoom settings added.

2026-10-06 audit follow-through (in progress): Claude's read-only audit found
no demonstrated wrong translucent submission order in shipped scenes. Keep
actor matching conservative pending native evidence; model coverage is small.
Collected fresh screen-cell counts in tools/claude-coverage-summary.txt:
Pewter flat_solid8/foreground4, Center16/2, Bedroom18/0, Route1 0/0, Viridian0/0.
These are screen samples, not whole maps; blank/boundary walls inflate counts.
Claude diagnostic copied via browser code block as local uncommitted patch
(tools/claude-actor-diagnostic.patch); native build passes. Header counts needed
git apply --recount and terminal newline. FRLG_ACTOR_DIAG logs once per second,
normal runs are unchanged (fresh disabled-control BMP byte-identical to
accepted d272ae8 idle). Native town idle/all four walking directions, Forest,
gate room and Center: 24 diagnostic windows, zero tall unmatched in all windows.
Up-walk also matched one 32x32 actor draw (four active signatures, three tall).
Bike/surf variants not yet covered; this is evidence for tested scenes only.
Numeric actor results: tools/claude-native-actor-audit.txt. No broad matching fix.
Highest-value measured coverage candidate: General 0E6/0E8/0F0/0F8 flat fence
around Pewter flower bed. Fresh grid/headers/TOP-row metadata supplied cloud
(tools/claude-pewter-geometry.txt; TOP/KEYED metadata matches each of16 rows).
Cloud implementing off-by-default model trial, no commits/pushes/PR; patch via
copyable diff. Native trial required before default enablement/deployment.
Installed EXE still d272ae8/22094B... while diagnostic/model work is pending.

2026-10-10 Let's Go 3D battle stage (Rust prototype, deployed): Settings G
`battle_3d` (default on, config.ini). FireRed battle engine unchanged; only
presentation. Stages from Let's Go bin/archive/background btl_N_* (full
trainer arenas; btl_F_* are open rings for the overworld and are not used):
routes/forest btl_N_grass, Pallet btl_N_road, Viridian btl_N_tokiwa, Pewter
btl_N_nibi, plus btl_sky. tools/letsgo/import_letsgo_battle.py writes
assets/letsgo/battle/*.glb + battle.json. src/battle_stage.rs: stage at
(4000,0,4000), player mon/foe/Red trainer spots, intro sweep foe close-up ->
over-the-shoulder view, idle drift, hit shake, field fog pushed out on stage,
compact translucent HUD (cards top, last two log lines + buttons bottom, field
HUD box hidden). models.rs: battle Pokemon on layer 0 at stage spots with real
clips (Attack/Hurt/Faint reactions), preview cameras off while on; Role::Trainer.
G off restores the flat panel. Capture: --route --at 6 33 1.5708 --slice-auto
--field-scripts --capture (stills skip the intro). Deployed EXE SHA256
7E3DD63B9FC2AD4B816F86B0A1C4A469F74AD7BD4D1EF119BB8AE4DFFBDECD19.
Next: move animations/effects (bin/battle/waza), ball-throw intro (Red
ba0030_ball01), send-out entrance, Charmander flame material, mipmaps.

2026-10-10 Let's Go shading pass (Rust prototype, deployed): Settings H
`letsgo_shading` (default on, config.ini). gf2glb.py / import_letsgo_kit.py /
char2glb.py now keep vertex colours + UV1 and write each Let's Go material's
real texture set and parameters as glTF material extras `lg` (kind, texture
slots, discard, shadow/rim colours, scroll). src/letsgo_material.rs swaps those
StandardMaterials for FieldMaterial (ExtendedMaterial,
assets/shaders/letsgo_field.wgsl): per-pixel grass/soil blend (vertex colour +
greenblend noise), cliff/rock top-vs-side by normal, ramp-lit trees (Texture02
light ramp x Texture03 inner mask) with rim light and outward canopy normals
baked by gf2glb.canopy_normals, dithered foliage fade near the camera, teal
Let's Go shadow colour, cloud01 projected cloud shadows, animated sea and shore
waves, character/Pokemon toon (LightTblTex R/G bands, ColSdw0Tex or
DeepShadowColor, AmbientTex), Charmander's PokeFireCore flame (fire mask pass
skipped). btl_sky dome follows the field camera outdoors (V flipped, unlit).
All file/glTF textures get CPU mipmaps (alpha coverage kept) + 8x anisotropy;
ramps clamp. Scene output alpha forced to 1 (scene is composited via a UI
image). Tonemapping now None by default (FRLG_TONEMAP=aces for ACES). H off =
old hard-split textured look. Deployed EXE SHA256
26C7C1DF6E287E1D8D2855BB38AD3E2179B1A15CA10E8729F40BD1E1E3BEC620; deploy must
also copy output/assets/shaders. Known: FireRed kit blocks (hedge cubes, grey
path squares, signpost) still on Route 1; battle backdrop Tree04 very dark;
windows black; Charmander back has a pale patch.
