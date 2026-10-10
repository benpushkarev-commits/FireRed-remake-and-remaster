# FireRed native Rust exploration and opening-battle slice

Latest encounter integration (2026-10-09): regular land steps now use original
metatile encounter attributes, including caves, with source cooldown/private rate
RNG, main 16-bit slot/level draws, flute/Cleanse Tag/lead ability modifiers, and
Repel expiration through EventScript_RepelWoreOff. Enabled with existing
field_scripts and battle_slice settings. Initial placement and map entry do not
roll encounters; map entry and battle start reset immunity. Private encounter
state is transient (host session/reset seeds it); Repel vars retain existing saves.
Native wild creation preserves source nature/personality/random-IV generation;
unsupported battle species/moves are reported instead of substituted. Captures
retain generated identity, consume a real Poke Ball from the native bag, and saves
wait for battle resolution. A full party routes captures through the original
current-box/circular PC search into 14 persistent 30-slot boxes. Wild flee odds, repeat attempts, Run Away/Smoke Ball,
basic ball bonuses, HP/status catch odds and shake RNG follow the pinned source. Wild
battles continue the persisted global RNG stream after encounter generation and return
their final state/call count to the field stream when resolved.
Net, Dive, Nest, Repeat, Timer, Luxury and Premier Ball multipliers also follow the
source, using imported target types, persistent caught state, level and battle turns.
Safari Ball still needs Safari catch-factor setup, and the current UI still throws only
a Poke Ball pending the original bag-selection flow.
Losing an integrated wild battle applies the original badge/highest-level money loss,
heals the party and returns to the saved `setrespawn` checkpoint when its recovery map
is imported.
Ordinary non-forced field steps also advance the persistent five-step poison counter.
Regular and toxic poison remove one HP on each wrap; poison fainting clears status,
applies the source friendship loss, preempts same-step encounters, and uses the same
checkpoint whiteout when the entire party faints.
In battle, Poison Sting uses its source secondary-effect draw/chance and imported
Poison/Steel/Immunity protection. Regular and toxic poison apply their source end-turn
damage, and residual faints award outcomes and experience through the normal bounded
battle path. Ember uses its imported secondary chance and source Fire type, Water
Veil, Shield Dust and existing-status prevention. Burn deals max HP / 8 each turn and
halves physical pre-+2 damage unless the attacker has Guts. String Shot lowers Speed
by one stage. Thunder Shock uses its imported secondary chance with Limber and Shield
Dust prevention. Paralysis quarters staged Speed, affects move order and the matching
trainer-AI speed branch, and makes its source one-in-four pre-PP action check. Badge,
item, weather and ability Speed modifiers remain unported. Sing follows the source
target-check and accuracy order, assigns a two-to-five sleep counter, and prevents
actions/PP use until wakeup; Insomnia, Vital Spirit, Soundproof, Early Bird and Uproar
are represented. Safeguard side state, Sleep Talk, Snore, other status-causing moves
and broad status/ability interactions remain unported. Harden's one-stage Defense increase and cap are executed,
and source moves with no accuracy command consume no accuracy roll. Opponent Harden AI
uses source last-move power/type and physical-type-list scoring. Many other AI commands
remain unported.
Sand Attack lowers the target's Accuracy stage, and hit checks use the source
Accuracy/Evasion stage fractions. The related AI status predicates are present for the
bounded early-battle state; Foresight, Protect, weather, held-item and ability accuracy
modifiers remain unported.
Damage uses FireRed's type-based physical/special split, including separate Special
Attack/Special Defense stages and critical stage bypass. It then applies same-type
attack bonus and the complete Generation III type chart in source integer order,
including distinct dual types, cancellation, immunities and effectiveness messages.
Levitate, Wonder Guard, Foresight, dynamic type changes, Struggle, burn, damage
items/abilities, weather, screens and doubles remain unported.
Native saves retain source-sized Pokédex seen/caught state across the full internal to
National species mapping. Wild encounters mark seen, captures and script gifts mark
caught, and original Kanto/National count specials read those flags.
The bounded trainer fixtures preserve source trainer mode/result/defeated-flag handling,
share the persisted global RNG stream, synchronize the first usable party Pokemon, and
apply early-rival healing or regular whiteout recovery. Only complete source single-mon
teams are enabled: the three Oak-lab rivals and Bug Catcher Sammy. Trainer opponents are
registered as seen, and wins award their source class/last-level prize with Amulet Coin
doubling. Multi-mon and unimported trainer teams fail explicitly instead of being
shortened or substituted.

This remains a partial baseline. No egg/cycling/forced-avatar state, surfing,
roamers, Unown/header overrides or Tower ghost/Safari initialization. Full battle
mechanics, complete trainer parties/switching/prize dialogue, a ball-selection bag UI, Safari Ball battle setup,
PC management/withdrawal, Pokédex screens, remaining field step counters and the full whiteout recovery cutscene remain incomplete. Free/diagonal movement is experimental;
new encounter behavior is UNVERIFIED against original C/frame timing. Headless
library checks and packaged --check exercise the native walking/Repel bridge.

A separate Windows Rust/Bevy application. Pallet Town, Route 1, the player's
house (both floors), rival's house and Oak's lab use imported FireRed map data.
The packaged game runs native Rust simulation, with no guest CPU, ROM or C++ runtime.
It does not touch the installed remaster's saves, configuration or executable.

## Play

Run `run.bat` or `output/FireRed Native 3D Prototype.exe`.

- WASD / arrows: movement. Shift: run outdoors.
- Space / Enter: interact and advance dialogue.
- Enter doors facing north; use stairs and the southern indoor exits.
- North from Pallet Town leads to Route 1; south returns.
- Mouse: third-person look; WASD follows the camera continuously at any angle. Combine keys for diagonals; release them to stop between tiles.
- Escape releases the cursor; click to capture it again. R resets the view.
- Q / E also orbit the camera. No zoom controls.
- Tab (or F1): settings; 1 toggles orbit, 2 toggles smoothing, 3 switches third-person/classic view, 4 toggles diagonal grid movement, 5 toggles free movement, 6 toggles original textures, 7 toggles music, 8 toggles the opening-battle slice, V toggles Red's imported playable model; saved in output/config.ini.
- B: open the isolated opening rival battle. In a walking wild battle, 1-4 use moves, C throws one Poke Ball from the native bag, and 5 tries to run. Buttons also accept mouse clicks. R restarts only the fixed rival fixture; Escape returns after a wild battle is resolved.
- Escape closes dialogue/settings/battle, releases the cursor, then quits when already released. Music is optional (Tab then 7), off by default. Exploration music pauses during the battle; battle audio is not implemented.

## What is authentic, and what is unfinished

Map cells, elevations, collision bits, behavior attributes, NPC definitions,
connections, warp destinations and dialogue come from pret/pokefirered revision
`037335f4c725d7c9aecdac87066f2002b4bd7e14`.
The importer independently checked all 1,990 cells and their used behavior
attributes against the local USA rev0 ROM (SHA1 41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc).
Source hashes and revision are packaged in data/provenance.json.

Rust ports cover directional collision, elevation rules, turn-in-place,
16-tick walking, 8-tick running, running restrictions, 32-tick ledge jumps,
map connections, door entry, stairs, dialogue locks and the clerk's one-time Potion.
The simulation owns movement completion and runs independently of rendering.
Source references: src/event_object_movement.c (collision and movement actions),
src/field_player_avatar.c (input and running), src/field_control_avatar.c
(door handling), src/metatile_behavior.c and the imported map scripts.
These are selected handwritten ports, not an automatic translation of the whole decomp.

This starts at an explicit exploration fixture: running shoes enabled, intro
and sign-lady scene skipped. NPCs are static snapshots; selected dialogue branches
and the Potion event have dedicated handlers. There is no general script VM,
story progression, world-connected party, encounters, healing, save/load or persistence of
item flags across restarts. Door animation is simplified; exact frame parity
has not been verified against a running original game. Ledge landings add a
walkability safety check. Map edges outside this six-map slice are closed.

Geometry remains deliberately simple. Original tile artwork textures the ground,
water, flowers, indoor floors, roofs, walls, windows and doors. Red is a rigged
3D model with basic authored idle, walk and run cycles and his source materials.
Trees remain solid 3D abstractions coloured from the original greenery palette.
No camera-facing sprites or billboards. Cosmetic art and building props never
determine collision; raw imported cells do. Interiors approximate furniture.
The source maps are authentic; the presentation is an interpretation.

### Animated slice models

The local `pokemon assests` folder includes the Let's Go Red model archive.
Its FBX was converted to `assets/models/red/red.glb`; the original FBX, Collada,
and separate material textures are retained in `assets/models/red/source/`.
The prototype loads the complete GLB scene, including its skeleton and skin weights.
Idle, walk and run are basic authored loops, with short transitions driven by actual
movement. Red stops walking against a wall and returns to idle; Settings pauses
playback. V switches the imported player model; A switches animation. Both persist
in config.ini (`red_model`, `character_animation`). The scene renders at 1280x720.

T / `trainer_models` switches matching source NPCs to Blue, Brock, Youngster,
Camper, woman, fat man, scientist, clerk, girl and old man models. Their imported
rigs have the same three basic clips. Original actor IDs, movement, visibility,
interactions and collision remain authoritative; unsupported graphics retain the
block representation. Blue's scripted appearances require the existing M actor
scenes setting. There is no Oak model in the supplied archives.

P / `pokemon_models` switches battle portraits to live 3D previews for Bulbasaur,
Charmander, Squirtle, Pidgey and male Rattata. They have a subtle planted idle sway;
attack, faint, capture and original skeletal animations remain future work.
Unsupported species and unloaded scenes use the existing battle sprites. Pokémon
world actors also use a matching model when the source object has those graphics.
This does not add invented wild spawns or change encounter rules. Charmander's
Collada import retains meshes/materials; all five initial Pokémon previews are
static meshes animated at the root. Their source archives retain the original rigs.
Bulbasaur's extended attack-vine meshes are excluded from the idle preview so they
do not appear as spikes or affect sizing. Charmander's flame uses the supplied alpha
mask with an orange emissive material. Preview cameras render only while in battle.

Rebuild assets locally with Blender 5.2:
`blender -b --python tools/import_slice_models.py -- "C:/Users/benpu/Desktop/pokemon assests"`.
The importer preserves FBX/SMD rigs, names clips, grounds and sizes models, and
embeds textures in each GLB. `assets/models/slice-models.json` records sources.
These are custom placeholder movements, not extracted original game animations.
The old `import_red_model.py` static-pose baker is superseded by this importer.

Silent visual QA supports `--capture <path> --free-view --model-walk` or
`--model-run`, and `--model-key <manifest-key>` to inspect a supplied model.

## Isolated opening rival battle

The battle is native Rust, based on the same pinned FireRed source. It uses
level-five Charmander versus the original rival's level-five Squirtle, with
Scratch/Growl and Tackle/Tail Whip from their original learnsets. The player's
starter is an explicit reproducible fixture: Hardy nature, all IVs 31, EVs 0.
The rival uses the original trainer IVs, encoded-name personality calculation,
nature and stats. This is not a random newly generated starter or a save import.

The original AI scripts are imported as instructions and interpreted by Rust.
The three trainer AI passes, scoring, simulated damage, move limitations for PP,
random calls and original tie-breaking are retained for this bounded matchup.
Damage preserves the original integer arithmetic, critical/stat-stage rules,
physical/special split, STAB/type effectiveness, 85-100 percent damage variation,
accuracy and first-battle tutorial flags.
In particular, accuracy checks test the MOVE_NONE power before resolving
ACC_CURR_MOVE; the source's resulting first-battle quirk is deliberately retained.
Win/loss, PP, stat floors, HP, the damage/stat tutorial messages, trainer-run
restriction, and single-participant experience/EV gain work. Victory grants
70 experience, one Defense EV, and level six with recalculated stats/HP.
Tutorial wording and turn presentation are simplified; combat is turn-stepped.

Verification is against original C, not just expectations written for Rust:
tools/check_battle_source.py compiles copied original AI handlers, AI processing
and selection, CalculateMonStats/ModifyStatByNature, accuracy/critical handlers,
and the original physical damage branch into a host-only reference executable.
Its host adapters supply the supported state and encode 32-bit script addresses
as offsets. The resulting 980 AI, 13,454 damage, 2,048 accuracy/critical and 400
stat vectors match Rust, including AI choices/scores/RNG state and call counts.
The reference C is only an authoring check and is not linked into the game.
Source hashes are in battle-provenance.json and battle-reference.json; the two
battle sprites come directly from the verified local ROM and have separate hashes.

This is a bounded port, not a complete battle engine or whole-ROM parity claim.
No lab cutscene/starter selection, general bag/party battle menus, switching,
most status moves, weather, broad abilities, additional moves/species, complete trainer
parties/switching, most money/story rewards, evolution or battle save/load yet. Walking wild battles share
the selected world party member and support source flee/basic-ball catch rules, but
the fixed rival fixture remains isolated from exploration. Unsupported AI
instructions or exhausted moves fail explicitly; there is no generic substitute
AI or Struggle implementation. The game does not reproduce per-frame background
RNG calls, GBA animations or sound; seeds are compared at battle-command boundaries.

Re-import: python tools/import_battle.py; python tools/import_battle_art.py.
Regenerate the original-C vectors: python tools/check_battle_source.py.
The sparse pinned source checkout and MinGW compiler are needed for authoring,
and the existing verified ROM only for sprite extraction. Runtime needs none.
tools/check-battle.ps1 runs the three exploration replays plus the actual-key
battle win/return/restart replay and captures battle, turns, win, loss and settings.

## Whole-game source-port foundation (2026-10-08)

The main direction is now an editable native Rust translation of the pinned
original FireRed C/data/scripts, preserving rules and quirks. Detailed behaviour
comparisons for new ports are deferred; **new core behaviour is unverified**.
The existing 3D/free-movement presentation remains an exploration experiment.

`src/core.rs` adds renderer-independent source-backed RNG, Pokémon creation
(personality/OT-ID/IV selection, initial moves, friendship and ability slot),
all six growth tables, level/stat recalculation including Shedinja and the
original HP-underflow bug, a six-member party, original bag pocket capacities,
one-stack item rules and automatic TM Case/Berry Pouch insertion, flag/variable
storage, 14 source-sized PC boxes, and versioned native JSON persistence. Full-party
`givemon` and capture use the original circular box scan, restore PP before boxing,
and update the destination box variables. Special flags/variables are
transient. Source-return quirks are retained (FlagSet/FlagClear return false).
Invalid guest indices fail safely; quest-log record/playback is not ported.

`tools/import_core.py` imports 411 species table entries (386 ordinary species
plus 25 legacy Unown entries), all 356 move entries including MOVE_NONE, all
375 positional item entries including reserved slots, complete level-up
learnsets, nature modifiers and growth tables. Original C table expressions
and symbol aliases are evaluated by an authoring-only compiler; no C is linked
into the Rust runtime. Full remaining table fields are retained for later ports,
but importing a move/item does not implement its effect. Provenance and source
hashes are in `data/core-provenance.json`.

Settings **9: Native save/load (restart)** persists `native_state=1/0` in this
application's config.ini (default off). Enable it, then restart to load/save
exploration location/facing, running-shoes fixture state, the Route 1 Potion
and its event flag via `output/native-state.json`. Saves occur at completed
movement/dialogue boundaries; fractional free movement resumes at the logical
cell. Invalid saves are preserved and reported on screen, with writing disabled.
Automated captures/replays bypass user saves. Core party/RNG/bag/event state
is serialized, but the isolated rival fixture still does not share party or
rewards with the world. The existing exploration start still skips the intro;
this is not the original story/new-game experience. Fixed session seed/trainer
ID lower half remain explicit adapter fixtures, not original startup timing.

Not ported in this batch: full BoxPokemon data access/encryption/checksums or
GBA .sav compatibility, OT/nickname/met/ribbon/contest metadata, PC menus/withdrawal,
full NewGameInitData side effects, field script VM, starter scenes, encounter
engine, item/move effects, complete battles and original menus/presentation.

The build runs `firered-core-check`: basic creation at levels 1/5/50/100 for
every imported species, party/bag/event integration and native save overwrite/
reload. The packaged application's `--check` also checks exploration adapter
save/reload and corrupt-file preservation. These are startup/integration checks,
not original-C behaviour comparisons. The check executable is packaged as
`output/FireRed Native Core Check.exe`. No installed C++ files are changed.

## Original field-script batch (2026-10-08)

`tools/import_field.py` now imports the pinned game's complete event-script
archive using its original assembler macros: 128,252 bytes, 9,731 labels,
4,580 native text entries, 213 command-table entries and ten standard scripts.
Sources include original map scripts, shared scripts/text, generated map/local
object/layout/heal identifiers derived from original JSON, and source hashes.
The host assembler emits only data. Script pointers become archive offsets or
explicit native import tokens; text payloads live as native strings, selected
by the original FireRed/rev0 assembly conditionals. This is source-backed script
data, not byte-for-byte original ROM layout or executable GBA code.

`src/field.rs` ports the script context/stack, control flow/standard calls,
comparison dispatch, locals, variable/flag/trainer-flag commands, RNG, item/bag
commands, party count, money and text-buffer/color handling. It retains the
source's silently refused twentieth stack push, immediate-only addvar versus
variable-aware subvar, and script item quantities narrowed to u8. Native
requests expose locks, messages, waits, delays, sounds, yes/no and specials.
Unsupported original opcodes, pointers and specials stop explicitly.

Settings **0: Original field scripts (partial)** stores `field_scripts=1/0`
(default off) and switches NPC/sign interactions to this VM in the existing
six-map exploration fixture. Its adapter freezes movement while scripts run,
shows original dialogue with item buffers, handles yes/no input (1 yes,2 no),
and connects original item/event state to native persistence. Unsupported events
discard their state changes and show an unavailable message; technical details
are logged. No unsupported command is silently converted to generic story logic.
Script execution state is transient; autosaves wait until an event finishes.
Core flags/party/money changes now participate in the persistence fingerprint.

The display remains an adapter: fixed RED/BLUE names, a readable POKEMON font
fallback while original accented source text is retained, instant manually advanced
pages, neutral static NPC presentation, no typewriter/fanfare timing or animation
parity. Sound requests are silent, and audio/fanfare waits are acknowledged by
this muted adapter. Delays use the existing source-derived tick rate. Quest-log
record/playback, dynamic placeholders/most specials, original actor movement,
automatic map scenes, starter/intro flow, party giving/PC/dex integration and
battle/encounter commands remain unsupported. `HealPlayerParty` itself is ported
(HP/status/PP bonuses), but full healing scenes still need their other commands.

Basic integration executes original `EventScript_ResetAllMapFlags` and both
Route 1 clerk branches. `--field-smoke` runs those clerk branches through actual
native key-input systems with exactly one Potion; it bypasses user saves/config
writes and forces silence. The build runs this check and packages the field data
and provenance separately from the installed C++ game. New VM behaviour remains
**unverified against original C**; no detailed comparison pass was added.

Final batch validation: build/package,31 retained library tests,packaged --check
and the hidden original-clerk key replay pass (both branches,one Potion). Fresh
Settings/source-dialogue views checked,including the final font fallback.
Installed C++ executable/config still match their preservation hashes.

## Build and validate

`powershell -ExecutionPolicy Bypass -File tools/setup.ps1` installs an isolated
toolchain on first use. `build.bat` runs headless library tests, builds and packages
an optimized development EXE and required MinGW DLLs. `build.bat -Release` uses
release mode. Cargo.lock pins dependencies; existing project MinGW supplies the linker.

`powershell -ExecutionPolicy Bypass -File tools/check.ps1` runs the hidden native
original, diagonal and free-controller input replays and captures eight fresh GPU-rendered views. The replay travels
through Pallet Town, both house floors and Route 1, checks conversations and
ensures the Potion is received once. Reports live in output/smoke-result.json;
logs and captures live in captures/. The replay routes with the same Rust
simulation, so it is an integration check, not an independent parity oracle.

To reproduce imports, run tools/fetch-reference.ps1, then run tools/import_world.py
with Python 3. The importer needs the existing local ROM and symbol table for
verification; normal builds and gameplay use checked-in JSON and need neither.
reference/ is an ignored sparse checkout. Do not edit generated JSON to change
source behavior without also updating the importer and provenance.

## Verified 2026-10-08

24 headless tests passed; packaged-data validation passed. Native replay passed
130 moves and four conversations over 2,582 simulation ticks, with exactly one
Potion. Fresh Pallet, Route 1, indoor, orbit, dialogue and settings views inspected.
Installed remaster executable and config hashes unchanged.

Next: port the original field script interpreter and its dependencies so the
starter/story sequence can create the real world party; expand the original
battle interpreter and encounter flow afterward. Detailed parity work is deferred.

Third-person camera proof of concept (2026-10-08): perspective follow camera,
mouse yaw/pitch, camera-relative eight-way grid movement, approximate scenery obstruction
shortening and focus-aware cursor capture. Enabled by default; F1 then 3 restores
the classic view. Settings persist with third_person=1/0. Camera controls are
paused in Settings. Native replay uses classic input to keep its original routes.

Diagonal extension verified 2026-10-08: changes the Rust movement core and camera
input mapping; original decomp files are unchanged. Enabled in third-person mode
by default, switchable with F1 then 4 (diagonal_movement=1/0). Opposite keys cancel.
Both cardinal paths around each diagonal must pass collision checks, preventing
wall/NPC corner cutting and elevation bypass. Doors, stairs and ledges require
cardinal approach; diagonal interaction works with clear side tiles. Diagonal
steps use 23 walking ticks or 11 running ticks to approximate equal world speed.
Eight directions still use cell-to-cell movement, not analog free movement.
Original native replay passed 130 moves; added actual W+D input replay passed
133 moves and the same four conversations, stairs, doors and one-Potion checks.
Fresh diagonal character view and updated Settings screenshot inspected.

## Continuous movement (2026-10-08)

Enabled by default with third-person mode. F1 then 5 switches free movement;
F1 then 3 restores the classic grid camera/controller. Free movement takes
priority over the diagonal-grid option. Own config.ini stores free_movement=1/0.
The controller lives in src/free.rs without a renderer dependency. Position and
heading are floating point; keyboard intent is normalized and rotated directly
by mouse-camera yaw. Movement stops on release and has no tile turn delay.
A circular body with short swept substeps slides along solid cells, prevents
corner cutting/tunnelling and respects elevations, directional barriers and NPCs.
Walking/running speeds retain the original 16/8-tick-per-cell rates; running
restrictions and dialogue locks still apply. Presentation follows the continuous
position and heading. Free movement is a new PC behavior, not original-game parity.

Doors use small approach triggers; stairs/exits use center proximity with a
re-entry guard. Free position is mapped to a logical cell for existing game rules.
Map crossings preserve fractional offset; warps reposition at their original
landing cell. Nearby, facing NPC/sign interactions require a clear line of sight.
Ledges automatically start the existing fixed-duration jump and finish at the
original landing cell. These event adapters replace the grid arrival assumption.
This is flat-ground third-person movement; vertical terrain/physics are deferred.
Collision still follows source tile geometry, not arbitrary 3D model meshes.

24 headless tests pass. The free-mode native replay verified an arbitrary camera
angle, fractional movement, 131 route steps, four conversations, both house
floors, Route 1 and exactly one Potion over 2,357 simulation ticks. Existing
four-way and eight-way native replays remain part of tools/check.ps1.
Free-mode report: output/free-smoke-result.json. Fresh free/Settings views checked.

## Original artwork and optional music (2026-10-08)

12 baked textures are shipped in assets/textures. tools/import_art.py decodes
source indexed tiles, palette selections and flip flags, and composites them
into ground surfaces and small building texture patches. Tiles, metatiles and
palettes are cross-checked against the local ROM; hashes are recorded in
 data/art-provenance.json. Colours for Red and trees live in data/art.json.
Interior floors use plain floor artwork, avoiding flat furniture stickers.
Hidden sides remain simple inferred geometry; this is not automatic 2D-to-3D
reconstruction. Red and the scenery have no sprite animation.

F1 then 6 toggles source_textures; enabled by default. F1 then 7 toggles music;
music=0 by default. Music stops when the window loses focus. The two offline
WAVs use the local ROM's song IDs 300 and 291 (Pallet Town and Route 1), rendered
with pinned gba-audio-tools revision 45e84ba8a5a47b22e56dac7e4e87b1ac605bcb19.
Windows plays them as simple loops; there is no music interpreter or GBA runtime
inside the application. Interiors currently reuse the Pallet theme. The offline
render is not a claim of exact hardware audio fidelity or seamless loop timing.
All automated checks force silence, even if the user's music preference is on.
The WAVs were checked for valid stereo PCM, nonzero samples and source song pointers;
audible playback was not auditioned during silent QA.

To reproduce assets: fetch the pinned pret reference with tools/fetch-reference.ps1,
then run tools/import_art.py with Python 3 and Pillow. For music, run
 tools/fetch-music-reference.ps1 and tools/import_music.py; the latter builds its
local offline C renderer with existing MinGW when necessary. Runtime builds use
baked PNG/WAV files, so Python and extraction tools are unnecessary for playing.
Music tool source/license: https://github.com/mudassarzahid/gba-audio-tools
Reference tools remain in ignored reference/; neither tool is linked into the game.

Validation: 24 simulation tests and all three native replays passed; packaged
asset validation passed. Fresh front, rear, indoor, Route 1 and Settings views
inspected; corrected final front/rear/indoor and texture-off views inspected.
Existing installed remaster executable/config hashes unchanged.

## Starter choice and dynamic battle integration (2026-10-08)

- Core learnsets: fixed `tools/import_core.py` to extract all 411 species learnsets
  with mapped move IDs from `level_up_learnsets.h` without macro stripping.
- Battle starters: added Bulbasaur alongside Charmander and Squirtle with authentic
  front and back sprites extracted into `assets/battle/`.
- Dynamic party/battle bridge: `Mon::from_pokemon` and `Mon::sync_to_pokemon` allow
  the battle engine to draw directly from `core::State` party and write back HP,
  experience, level, and EV rewards upon victory.
- Field script opcodes: added handlers for `givemon`, species/number string buffering,
  `warp`, `warpsilent`, door animations, `showmonpic`/`hidemonpic`, BGM fade/save,
  and `trainerbattle` modes.
- Oak's Lab: `PalletTown_ProfessorOaksLab_EventScript_ChoseStarter` awards the
  chosen starter and syncs rival counter-starter selection.
- All 33 unit tests, `firered-core-check`, and packaged `--check`, `--field-smoke`,
  `--scene-smoke` validation pass.
