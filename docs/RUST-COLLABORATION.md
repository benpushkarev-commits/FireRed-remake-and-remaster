# Faithful Rust port collaboration

Ben authorized Codex and Antigravity/Gemini collaboration on 2026-10-09.
Goal: complete a source-based native Rust FireRed baseline before redesigning mechanics.
Use pinned `rust-prototype/reference/pokefirered` source; retain source references and document gaps.
Imported maps and passing smoke checks do not imply complete story or battle behavior.
Keep the installed C++ game untouched. Keep audio muted. Preserve existing edits.

## Ownership for the current batch

2026-10-10 Codex Recover/Defense Curl/Water Sport/Teleport batch is packaged and the
build lock is released. Codex retains `src/battle.rs`, `src/field_screen.rs` and its
source-mechanics docs. The bounded executor now preserves the four moves' PP, RNG,
stage/status, global Fire-power and wild/trainer escape order for represented state.
The serialized release also compiled and smoke-checked Gemini's stable Fuchsia/Safari
handoff (180 maps, 186 textures, 751 actors, 102 triggers). Gemini-owned importers and
generated data remain stable; no overlapping edits were made.
Next Codex ownership remains `src/battle.rs` for the Leech Seed, Wrap/trapping and
Rapid Spin residual/cleanup batch; Gemini may continue non-overlapping importer/data
work without building.

2026-10-10 Codex imported damage-effects batch is packaged and the build lock is
released. Codex retains `src/battle.rs`, `src/field_screen.rs` and its source-mechanics
docs. Swift, Karate Chop, Absorb/Leech Life and Bubble/Rock Tomb now execute their
source-specific accuracy, critical, drain and damaging Speed-drop paths. Ordinary
integrated battles no longer inherit the Oak tutorial gates; lab rivals still do.
Gemini-owned importers and generated files remain unchanged and stable.

2026-10-10 Codex freeze/thaw batch is packaged and the build lock is released.
Codex retains its source-mechanics files. The bounded engine now covers damaging
freeze infliction and prevention, pre-action freeze RNG/action blocking, delayed
`EFFECT_THAW_HIT` clearing and thawing a surviving target after an effective Fire
hit. Gemini-owned importers and generated files remain unchanged and stable. Weather
state and broader freeze/thaw move exposure remain explicit later integration work.

2026-10-10 Codex Safeguard side-state batch is packaged and the build lock is
released. Codex retains its source-mechanics files. The bounded engine now carries
the source five-phase side timer, duplicate-cast failure, symmetric expiry, executor
support and source-ordered blocking for existing damaging status secondaries plus
Sing. Gemini-owned importers and generated files remain unchanged and stable. The
curated battle data still needs broader move exposure before current fixtures can
select `MOVE_SAFEGUARD`; its AI selection remains a later integration batch.

2026-10-10 Codex damaging-Confusion/volatile-turn batch is packaged and the build
lock is released. Codex retains its source-mechanics files. The bounded engine now
supports Confusion's source secondary chance and prevention checks, two-to-five-turn
volatile counter, pre-action decrement, snap-out path and 40-power self-damage.
Gemini-owned importers and generated files remain unchanged and stable. Primary
confusion moves and item cures remain explicit later batches.

2026-10-09 Codex Sing/sleep-turn batch is packaged and the build lock is released.
Codex retains its source-mechanics files. The bounded engine now supports Sing's
source prevention/accuracy order, sleep duration, action denial, wakeup, Early Bird
and Uproar behavior. Gemini-owned importers and generated files remain unchanged and
stable.

2026-10-09 Codex Thunder Shock/paralysis batch is packaged and the build lock is
released. Codex retains its source-mechanics files. The bounded engine now preserves
paralysis secondary chance, prevention, quartered Speed and one-in-four action denial.
Gemini-owned importers and generated files remain unchanged and stable.

2026-10-09 Codex Ember/burn batch is packaged and the build lock is released. Codex
retains its source-mechanics files. The bounded engine now preserves source abilities,
damaging-move secondary chances and burn damage/Attack behavior; Poison Sting shares
the same secondary-effect path. Gemini-owned importers and generated files remain
unchanged and stable.

2026-10-09 Codex STAB/type-effectiveness batch is packaged and the build lock is
released. Codex retains its source-mechanics files. The bounded engine now uses the
complete source type chart for actual damage and AI faint checks, including dual types
and immunities. Gemini-owned importers and generated files remain unchanged and stable.

2026-10-09 Codex Generation III physical/special damage batch is packaged and the
build lock is released. Codex retains its source-mechanics files. Move types now select
Attack/Defense or Special Attack/Special Defense with source stage/critical rules.
Gemini-owned importers and generated files remain unchanged and stable.

2026-10-09 Codex Sand Attack/accuracy-stage batch is packaged and the build lock is
released. Codex retains its source-mechanics files. Sand Attack, Accuracy/Evasion stage
ratios and the required AI status predicates are complete for the bounded early-battle
state. Gemini-owned importers and generated files remain unchanged and stable.

2026-10-09 Codex last-move/type AI batch is packaged and the build lock is released.
Codex retains its source-mechanics files. Opponent Harden now crosses the source AI's
last-move, power/type and byte-list scoring branches using read-only metadata from the
existing core import. Gemini-owned importers and generated files remain unchanged and
stable.

2026-10-09 Codex Harden/no-accuracy-script batch is packaged and the build lock is
released. Codex retains its source-mechanics files. Defense-up execution, its stage
cap/damage effect and source zero-accuracy-script behavior are complete in the bounded
engine; deeper opponent stat-up AI scoring remains an explicit gap. Gemini's generated
data/importer handoff remains unchanged and stable.

2026-10-09 Codex String Shot/speed-order batch is packaged and the build lock is
released. Codex retains its source-mechanics files. Bug Catcher Sammy's String Shot,
Speed stage turn ordering, paralysis quartering and the needed AI faster-target
branch are complete in the bounded engine. Gemini's generated data/importer handoff
remains unchanged and stable.

2026-10-09 Codex battle-poison batch is packaged and the build lock is released. Codex
retains its source-mechanics files. The bounded battle engine now covers Poison Sting's
secondary chance/immunity path and regular/toxic end-turn damage through residual
faints. Gemini's generated data/importer handoff remains unchanged and stable.

2026-10-09 Codex field-poison step batch is packaged and the build lock is released.
Codex retains its source-mechanics files. Persistent five-step poison damage, poison
faint friendship/status cleanup and all-party checkpoint whiteout are complete for
ordinary native field walking. Gemini's generated data/importer handoff remains
unchanged and stable.

2026-10-09 Codex trainer-fixture/Pokédex/prize batch is packaged and the build lock is
released. Codex retains its source-mechanics files. Only complete source single-mon
trainer parties are exposed; shortened multi-mon fixtures were removed. Supported
trainer starts now set seen state and wins pay source class/last-level prize money,
including Amulet Coin. Gemini's generated data/importer handoff remains unchanged and
stable.

2026-10-09 Codex source special-ball mechanics batch is packaged and the build lock is
released. Codex retains its source-mechanics files. Net/Dive/Nest/Repeat/Timer/Luxury/
Premier catch multipliers and world catch context are complete for regular wild battles;
Safari setup and the original bag selection UI remain explicit gaps. Gemini's generated
data/importer handoff remains unchanged and stable.

2026-10-09 Codex trainer field-state batch is packaged and the build lock is released.
Codex retains its source-mechanics files. The bounded trainer adapter now preserves
source mode/result/flag/whiteout and global-RNG behavior and rejects unimported teams
explicitly. Gemini's generated-data handoff remains unchanged and stable.

2026-10-09 Codex persistent-Pokédex batch is packaged and the build lock is released.
Codex retains its source-mechanics files. Gemini's generated-data handoff remains
unchanged and stable.

2026-10-09 Codex source-whiteout batch is packaged and the build lock is released.
Codex retains its source-mechanics files. Gemini's generated-data handoff remains
unchanged and stable; recovery destinations outside its currently imported maps stay
explicitly unsupported.

2026-10-09 Codex wild-battle RNG continuity batch is packaged and the build lock is
released. Codex retains its source-mechanics files. Walking encounters and their wild
battles now share the persisted source RNG stream; Gemini's generated-data handoff is
unchanged and stable.

2026-10-09 Codex PC storage/full-party capture batch is packaged and the build lock
is released. Codex retains its source-mechanics files. Gemini's generated Celadon
handoff remains stable and no concurrent package was reported; Gemini continues to
own importer tools, generated world/data/assets and actors.

2026-10-09 Codex wild flee/catch mechanics batch is packaged and the build lock is
released. Codex retains main.rs, field_screen.rs, battle_screen.rs, lib.rs,
encounter.rs, battle.rs and core.rs for the next source mechanics batch. Gemini's
generated Celadon handoff remains stable; its existing Antigravity chat is currently
waiting on an unrelated user questionnaire, so no code/build overlap was introduced.
Gemini continues to own importer tools, generated world/data/assets and actors.

2026-10-09 combined Celadon/encounter batch packaged successfully. Build lock is
released. Codex keeps main.rs, field_screen.rs, lib.rs, encounter.rs, battle.rs and
core.rs reserved for the next mechanics batch; Gemini remains owner of importer tools,
generated world/data/assets and actors. Imported geography is not completed gameplay.

2026-10-10 Fuchsia City, Gym, and Safari Zone batch complete (180 maps reported):
Gemini completed all 19 new maps covering Fuchsia City and the entire Safari Zone (Center,
East, North, West, rest houses, Secret House), added secondary tilesets gTileset_FuchsiaGym
and gTileset_SafariZoneBuilding (186 baked textures), 751 actor objects, 102 triggers, and
maintained per-cell encounter_types ROM-verified across 103,723 cells. Generated data is
STABLE and validated 100% against World::validate() constraints in lib.rs. Codex-reserved
files remain untouched and cargo/build lock was strictly observed. Handoff ready for Codex
compilation and packaging.

2026-10-10 Routes 7 & 8, Underground Path (East-West), Routes 16-18 (Cycling Road),
and Routes 12-15 (Silence Bridge) batch complete (161 maps reported): Gemini completed
all 24 new maps connecting Celadon, Lavender, and southern Kanto corridors, added secondary
tileset gTileset_FuchsiaCity (167 baked textures), 680 actor objects, 99 triggers, and
maintained per-cell encounter_types ROM-verified across 92,018 cells. Generated data is
STABLE and validated 100% against World::validate() constraints in lib.rs. Codex-reserved
files remain untouched and cargo/build lock was strictly observed. Handoff ready for Codex
compilation and packaging.

2026-10-09 Celadon batch complete (137 maps reported): Gemini completed per-cell
encounter_types in import_world.py/world.json (bits 24..26 ROM-verified) and all
Celadon outdoor/interior geography (Gym, Dept Store 1F-5F/Roof/Elevator, Game Corner,
Prize Room, Rocket Hideout B1F-B4F/Elevator, Condos 1F-3F/Roof/Roof Room, Hotel, etc.).
Generated data (world.json, art, actors, field) reported STABLE in RUST-GEMINI-STATUS.md.
Gemini avoided lib.rs, main.rs, field_screen.rs, encounter.rs, battle.rs, core.rs, and
did not run cargo/build.ps1. Handoff ready for Codex compilation and packaging. Older notes below.


S.S. Anne / Route 11 / Diglett's Cave batch completed (93 maps reported).
Gemini proposed transferring main.rs to Codex; accepted for encounter integration.
Actual encounter trigger is in field_screen.rs, so Codex requests that file too.
This additional transfer is pending acknowledgment; Codex has not edited it.
Next proposed Gemini batch: Route 9 / Route 10 / Rock Tunnel / Lavender geography.
Keep the same importer/data ownership, avoiding main.rs and reserved core files.
Assignment delivered at the 02:20 UTC heartbeat; Antigravity access restored.
field_screen.rs acknowledgment remains pending. Codex added core Repel counter API.

- **Antigravity/Gemini:** Route 5 through Vermilion world expansion: map/tileset/actor/script imports and connected transitions. Own import tools and their generated data/assets; world integration in main.rs/actors.rs/field_screen.rs if needed. Do not change encounter.rs or battle.rs/core.rs. Record missing script specials and unsupported battles explicitly. Avoid fabricated quest success or placeholders presented as finished logic. Run import checks, compilation and basic smoke checks. Do not package while Codex is building; report readiness first.
- **Codex:** encounter.rs source-based rules and focused checks; audit gaps and integration. Own this task board and coordination notes. Avoid Gemini-owned importer/data/world files during the batch.

## Coordination

Before expanding scope, record requested file ownership here and read the other agent's status file.
Antigravity writes `docs/RUST-GEMINI-STATUS.md`; Codex writes `docs/RUST-CODEX-STATUS.md`.
Include completed files, source references, checks, blockers and next proposed batch.
Request an ownership transfer before touching another agent's reserved files.
Compile and basic checks stay required; detailed behavior comparisons may be deferred and marked unverified.
Continue useful batches within account/tool limits; do not bypass limits or create activity merely to consume usage.
