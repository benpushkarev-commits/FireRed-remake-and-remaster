# Rust port handoff — 8 October 2026

## Ben's latest direction

The main objective is now a bare-bones native Rust port of the whole original
FireRed game, providing an editable faithful baseline before redesigning it.
Use original source code and data, preserving rules and quirks. Do not invent
equivalent-looking mechanics or quietly substitute generic implementations.
Replacing GBA hardware dependencies is necessary, but should preserve behaviour.
Keep source references and make unsupported features explicit.

Ben explicitly prefers faster translation/integration now: defer detailed
behaviour comparisons until a later verification pass. Keep compilation and
basic startup/integration checks. Label new behaviour unverified until checked;
the previous successful slice is evidence for the method, not proof for the
remaining game. Do not repeatedly run extensive parity checks for every change.

Defer new control, presentation and gameplay redesign. Keep existing free movement
and 3D experiments available separately from the faithful baseline. Long-term Ben
wants a proper third-person 3D remake, later-game systems/Pokémon, and potentially
additional regions/games, but these are future directions, not the current scope.

Ben likes conversational discussion that helps him understand the tradeoffs,
followed by a clear scope and implementation when he says proceed. He requested
this fresh chat because the long voice conversation felt slow. The cause of the
lag was not established. This handoff is the initial context for the new phase;
do not interpret its broad goal as permission to claim or rush a complete port.
Initial new-chat task: absorb context and establish the first substantial batch.

Use Codex for this Rust effort. Ben previously explicitly rejected Claude and
subagent delegation; do not use them unless he changes that preference. This
overrides the legacy C++ project's preference for Claude cloud implementation.

## Two distinct projects in this workspace

The existing C++ remaster uses static recompilation of FireRed's ARM game logic.
Its installed executable is in C:/Users/benpu/Desktop/FireRedRecomp-esc-menu/
Pokemon Fire Red.exe. Preserve that executable, config, saves and pending C++ work.
Do not deploy the Rust application over it. The project root is not a Git repo;
the existing C++ game lives in its own child repository.

The separate rust-prototype/ application is native Windows Rust/Bevy, with no
guest CPU, runtime ROM or recompiled C++ dependency. It contains selected
handwritten source-based ports, imported original data, and new presentation/
movement code. It is not a full translation or a fully faithful original game.

Original readable source is pret/pokefirered C, data and script definitions,
not C++. Pinned revision is recorded in rust-prototype/README.md and provenance;
the sparse checkout is rust-prototype/reference/pokefirered (ignored).
Never read variants/*/generated/, project deps/, C++ build/ or fr_hd_fonts.cpp.
Read docs/STATE.md and rust-prototype/README.md first; use rg and focused reads.

## Completed exploration slice

Six imported maps: Pallet Town, Route 1, player house 1F/2F, rival house, Oak lab.
1,990 raw map cells and behaviour attributes checked against Ben's USA rev0 ROM.
Selected native ports include directional collision, elevations, 16/8-tick
walk/run timing, ledges, connections, doors/stairs, selected dialogue and the
clerk's one-time Potion. No full field script interpreter or story progression.
The exploration fixture grants running shoes and skips introductory scenes.
NPCs are static snapshots. No world party, encounters, catching, healing,
save/load or persistent event flags across restarts.

New third-person mouse camera and default continuous camera-relative WASD live
alongside optional cardinal/eight-way grid controls. Continuous movement uses
renderer-independent swept circular collision, wall sliding and source map/event
adapters; it is a deliberate redesign, not original movement parity.
Red is an unanimated block model; trees are solid abstractions, not billboards.
Twelve original-art textures use source palettes/tiles verified against the ROM.
Optional offline Pallet/Route 1 WAV music is off by default; audible playback
has not been auditioned. Keep audio muted in QA and add no zoom controls.

## Completed isolated opening rival battle

Playable from exploration with B. Level-five Charmander versus the original
rival's level-five Squirtle (Charmander's rival chooses Squirtle, not Bulbasaur).
Player is an explicit fixed Hardy / all-31-IV / zero-EV test fixture, not a random
starter/save import. Rival IVs, encoded-name personality, nature and stats come
from the original trainer setup. Four moves: Scratch, Growl, Tackle, Tail Whip.

src/battle.rs owns renderer-independent state, the bounded AI interpreter,
original three AI passes, scores/simulated damage/RNG/tie-breaking, integer damage,
crit/stat-stage rules, accuracy, opening tutorial flags, PP/HP/faint/win/loss.
Preserves the original ACC_CURR_MOVE/MOVE_NONE first-battle accuracy quirk.
Victory gives 70 EXP, one Defense EV and level six with recalculated stats/HP.
Original AI scripts are imported as data; unsupported instructions fail, with
no generic fallback AI or Struggle. src/battle_screen.rs is the native UI adapter,
with original ROM-extracted Charmander-back and Squirtle-front sprite images.
Exploration freezes and resumes independently. Rewards remain isolated from it.

No lab cutscene/starter selection, world-linked inventory/party, bag actions,
switching/catching, held items, major status, weather, additional species/moves,
money/story rewards, evolution or battle save/load yet. Tutorial text and turn
presentation are simplified; battle music/animations are absent.

Previous milestone verification: 31 library tests passed. Original C host oracle
compiles actual AI handlers/processing/selection, physical damage branch,
accuracy/critical commands and CalculateMonStats/ModifyStatByNature with scoped
host adapters. 980 AI + 13,454 damage + 2,048 accuracy/critical + 400 stat cases
match Rust (16,882 total); AI RNG state/call counts also match. This is bounded
command-level parity, not whole-ROM/frame timing parity. The original game's
per-frame background RNG advances are not reproduced. The reference C is an
authoring tool only, not linked into the runtime.

All four native input replays passed (cardinal, diagonal, continuous and battle).
Battle replay used actual key-input systems to win, return to the same world
position and restart. Win/loss/settings screenshots inspected; final UI fixes
prevent world HUD overlap and repeated text shaping. Sprite package hashes and
installed-game/config preservation hashes checked. No original install changes.

## Entry points and tools

Run rust-prototype/run.bat; packaged executable is output/FireRed Native 3D Prototype.exe.
WASD, Shift run, Space interact, mouse look. B battle; 1/2 moves; R restart;
Esc return. Tab (or F1) Settings, 7 music, 8 battle module. Ben has no function keys.
Own output/config.ini stores toggles, including battle_slice=1 by default.

Rust uses an isolated GNU toolchain under rust-prototype/tools/cargo and tools/rustup.
tools/build.ps1 sets environment, runs locked headless library tests, builds an
optimized development executable, packages data/assets/MinGW DLLs and validates it.
Do not change the global toolchain. Cargo fmt required sandbox escalation, which
was approved automatically; ordinary builds succeeded without it. Cache hard-link
warnings are harmless. Bevy is pinned by Cargo.lock; no runtime audio interpreter.

Authoring: tools/import_world.py, import_art.py, import_music.py,
import_battle.py, import_battle_art.py and check_battle_source.py.
Original C vectors: data/battle-reference.json. Source and sprite provenance files
are also in data/. tools/check-battle.ps1 runs four hidden input replays and five
battle/settings captures with audio forced silent. Read only new captures for QA.
Do not re-run extensive comparisons merely to orient the fresh chat: they passed,
and Ben's new priority explicitly defers detailed verification of future ports.

## Suggested first substantial step (not yet implemented)

Map original subsystem dependencies and establish the faithful Rust core boundary:
Pokémon data/creation/stats/party, inventory and persistent player/event state;
then broaden battle logic and port the script interpreters that connect systems.
This suggestion is not an agreed detailed implementation batch. Confirm scope
conversationally with Ben without reopening decisions already recorded above.

## Subsequent authorization and first batch (8 October)

Ben then authorized proceeding with as much of the original source port as
possible and handing off when useful manual testing is ready. The first batch
is now built/packaged: complete core tables, native Pokémon creation/stat/party
foundation, bag/event state and JSON persistence. Settings9 optionally connects
save/load to the existing exploration fixture; source scripts still do not
create a world party, and rival battle rewards remain isolated. See STATE.md
and README.md for exact scope/checks/limitations. New behaviour is unverified
against original C; compilation/basic integration passed. Next dependencies are
the original field VM/new-game story and general battle/encounter flow.

## Original field interpreter follow-through (8 October)

Ben confirmed the save/load test worked and asked to continue. The next batch
imports all original event scripts/macros/text and adds a renderer-independent
partial bytecode interpreter plus a switchable native NPC/sign adapter. Settings0
enables field_scripts; source Route1 clerk branches run through this interpreter.
Source story actor movement/map callbacks/starter/party giving and general
battle/encounters remain next. Imported script breadth does not imply all command
handlers are ported. Unsupported events are explicit and discard partial state.
Detailed new original-C comparisons remain deferred; see STATE/README for checks.

## Starter choice and dynamic battle integration (8 October)

Completed starter selection and party battle integration:
1. `tools/import_core.py` corrected so all 411 species now parse original level-up
   learnsets directly with mapped move IDs (previously all 411 were empty lists due
   to macro expansion).
2. Added Bulbasaur to `data/battle.json` and ROM-extracted PNG battle front/back sprites
   for Bulbasaur, Charmander, and Squirtle in `assets/battle/`.
3. Generalized battle engine: `Mon::from_pokemon` and `Mon::sync_to_pokemon` bridge
   between core and battle data; `Battle::from_matchup` plays any starter pairing,
   dynamically awarding calculated EXP and EV rewards.
4. Field interpreter extended with bytecode handlers for `givemon`, `bufferspeciesname`,
   `bufferleadmonspeciesname`, `buffernumberstring`, `warp`, `warpsilent`, `opendoor`,
   `closedoor`, `waitdooranim`, `setdooropen`, `setdoorclosed`, `showmonpic`, `hidemonpic`,
   `fadescreen`, `fadescreenspeed`, `playbgm`, `savebgm`, `fadedefaultbgm`, `fadenewbgm`,
   `fadeoutbgm`, `trainerbattle`, `dotrainerbattle`, and `gotopostbattlescript`.
5. Oak's Lab starter choice (`PalletTown_ProfessorOaksLab_EventScript_ChoseStarter`)
   gives chosen starter to player party, sets flags, and rival receives counter-starter.
6. 33 tests pass (including new starter matchup and script tests), `firered-core-check`
   passes, and package validation (`--check`, `--field-smoke`, `--scene-smoke`) passes.
