# Codex status — 2026-10-10

## Source Recover, Defense Curl, Water Sport and Teleport batch

COMPLETED/PACKAGED 06:39 local. Ported the four source-backed self/support effects
already present in the curated move data. Recover spends PP before its full-HP failure
and restores floor(max HP / 2), with a minimum of one and the ordinary HP cap. Defense
Curl sets `STATUS2_DEFENSE_CURL` before attempting the one-stage Defense increase, so
the marker remains set at the stage ceiling. Water Sport skips accuracy RNG, refuses
a duplicate after PP use, and halves Fire move power before damage calculation while
either active battler carries `STATUS3_WATERSPORT`.

Teleport spends PP before trainer or trap failure. Eligible wild-battle use ends the
battle without the ordinary flee speed/run-attempt RNG. The bounded gate covers Run
Away/Smoke Ball precedence, Wrap/escape prevention, Rooted, Shadow Tag, Arena Trap
with Flying/Levitate, and Magnet Pull against Steel. The focused check covers odd-HP
Recover rounding/full failure, capped Defense Curl plus its bit, Water Sport's no-RNG
and duplicate paths/global Fire power, trainer/trap Teleport failure, always-run and
Flying overrides, and integrated PP/executor behavior for all four moves.

The same serialized release compiled and packaged Gemini's stable Fuchsia City and
Safari Zone generated-data handoff (180 maps, 103,723 cells, 186 source textures, 751
actors and 102 triggers). `tools/build.ps1 -Release` exited 0: core startup, all 73
library tests, packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output
executable updated directly (SHA256
`EA300BD7EE714C2EBCE6071A23504AC4EE47063802903EB7B99872F6971F26B3`); no pending
executable. Native music remains 0 and installed C++ game is untouched.

Rollout is not yet in the curated executor, so it cannot consume Defense Curl's
double-power marker. Party switching and full volatile-status lifecycle, the
Ghost-without-Scope battle flag, and the original ability-specific Teleport message
remain gaps. Detailed original-C comparison is deferred; new behavior is UNVERIFIED
beyond source inspection and basic checks.

Next Codex batch: port the remaining curated residual interaction across Leech Seed,
Wrap/trapping and Rapid Spin cleanup. Gust's airborne multiplier and Low Kick's
weight-based power remain queued behind their missing battle-state inputs.

## Imported damaging-move effects batch

COMPLETED/PACKAGED 02:07 local. Ported four source-backed effect families already in
the curated move data. `EFFECT_ALWAYS_HIT` explicitly skips the accuracy-stage roll;
`EFFECT_HIGH_CRITICAL` advances Karate Chop from the ordinary 1/16 rate to 1/8.
Battle Armor and Shell Armor reject criticals before the RNG call, matching
`Cmd_critcalc` short-circuit order.

Absorb and Leech Life use `gHpDealt`: the attacker receives half the actual target HP
removed, rounded down and capped by missing HP. One point dealt therefore restores
zero in this source. Liquid Ooze reverses the same amount and attacker fainting is
resolved before target fainting. Bubble and Rock Tomb consume the source inclusive
secondary-chance draw after damage. Eligible Bubble procs lower Speed one stage while
Shield Dust, Clear Body, White Smoke, Substitute and the stage floor reject it. Rock
Tomb's 100% chance becomes `MOVE_EFFECT_CERTAIN` and bypasses those three abilities,
as the original `SetMoveEffect`/`ChangeStatBuffs` path does.

Ordinary `from_matchup` and wild battles now start with both Oak tutorial flags
complete, restoring normal accuracy and critical behavior. Integrated Oak-lab rivals
326-328 explicitly retain the first-battle flags. The focused check covers explicit
Swift accuracy bypass and RNG count, the 1/8 versus 1/16 critical boundary, armor
short-circuit, Bubble's inclusive proc, Clear Body, Rock Tomb's certain bypass,
drain rounding, Liquid Ooze and integrated healing. Serialized
`tools/build.ps1 -Release` exited 0: core startup, all 72 library tests, packaged
`--check`, `--field-smoke` and `--scene-smoke` passed. Output executable updated
directly (SHA256
`F8362153B3715F42E4F48A00DCC7F39E35F6BD5C842734FCCA6AC8FE4C9A81EF`); no pending
executable. Native music remains 0 and installed C++ game is untouched.

Substitute HP is not represented, so drain through a Substitute remains deferred.
Mist, Protect/semi-invulnerable target state, Focus Energy, critical held items and
the other critical-stage effects remain gaps. Detailed original-C comparison is
deferred; new behavior is UNVERIFIED beyond source inspection and basic checks.

## Source freeze and thaw batch

COMPLETED/PACKAGED 01:38 local. Ported the bounded freeze paths from
`battle_script_commands.c:SetMoveEffect`, `battle_util.c:AtkCanceller_UnableToUseMove`
and the move-end defrost state. Damaging freeze effects retain the inclusive chance
draw and source rejection order for Shield Dust, Safeguard, fainted target,
Substitute, Ice typing, existing primary status and Magma Armor.

After sleep handling, a frozen battler consumes `Random() % 5`; zero thaws and
continues, while other results block the ordinary move before confusion, paralysis,
PP use or last-move replacement. `EFFECT_THAW_HIT` still consumes the freeze roll but
defers clearing until after confusion/paralysis, so either later blocker leaves the
user frozen. A nonzero effective Fire hit also thaws a surviving frozen target.

The focused check covers infliction RNG and gates, ordinary and natural-thaw turns,
the delayed thawing-move stage, Fire-hit survival/damage/effectiveness conditions, and
integrated PP/last-move preservation. Serialized `tools/build.ps1 -Release` exited 0:
core startup, all 71 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`A4586627C34989EF913E3A5D716150024505A692DFACA8C173E7D7347881C94A`); no pending
executable. Native music remains 0 and installed C++ game is untouched.

Harsh-sun prevention remains deferred because the bounded battle state has no weather
field. The current curated `battle.json` exposes no freeze-hit or thaw-hit move to its
regular fixtures. Detailed original-C comparison is deferred; new behavior is
UNVERIFIED beyond source inspection and basic checks.

## Source Safeguard side-state batch

COMPLETED/PACKAGED 2026-10-10. Ported the side-wide state and timing from
`battle_script_commands.c:Cmd_setsafeguard`, `SetMoveEffect` and
`battle_util.c:ENDTURN_SAFEGUARD`. Either side can receive a five-phase counter; a
duplicate attempt fails without refreshing it, and the counter decrements at the end
of each completed round before expiring at zero. The bounded executor recognizes
`EFFECT_SAFEGUARD` for future imported users.

Existing damaging poison, burn, paralysis and confusion secondaries retain their
inclusive chance draw, then reject in source order through Shield Dust, Safeguard,
fainted target and Substitute. Sing checks Safeguard only after a successful accuracy
roll and PP reduction. Focused checks cover symmetric side protection, chance-RNG
consumption, duplicate casting, all five end-turn phases, post-expiry application and
the integrated primary-Sing path. Serialized `tools/build.ps1 -Release` exited 0:
core startup, all 70 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`65E5C079D809B26BF40A2CC8C0060FDD9C115F4FCF363EA9FDF22619D352C315`); no pending
executable. Native music remains 0 and installed C++ game is untouched.

The current curated `battle.json` does not expose `MOVE_SAFEGUARD`, so present early
fixtures cannot select it even though the executor path exists. Broad primary-status
scripts, status caused by abilities and Safeguard AI selection remain gaps. Detailed
original-C comparison is deferred; new behavior is UNVERIFIED beyond source inspection
and basic checks.

## Source damaging Confusion and volatile-turn batch

COMPLETED/PACKAGED 2026-10-10. Ported the `MOVE_CONFUSION` damaging secondary and
pre-action volatile paths from `battle_script_commands.c:SetMoveEffect`,
`battle_util.c:AtkCanceller_UnableToUseMove` and the matching battle scripts. The
inclusive 10% chance draw retains Serene Grace and source rejection for Shield Dust,
Substitute, Own Tempo, existing confusion, a fainted target and no effect. Successful
application assigns `(Random() % 4) + 2` turns without replacing primary status.

Before acting, the counter decreases; zero snaps out and continues the selected move.
Otherwise the source half-chance draw either permits the move or deals 40-power
self-damage from the battler's own Attack and Defense with the ordinary 85-100% damage
roll. A self-hit consumes no PP and does not replace last-move state. Focused checks
cover duration RNG, coexistence with burn, Own Tempo, self-damage and snap-out RNG.
Serialized `tools/build.ps1 -Release` exited 0: core startup, all 69 library tests,
packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output executable
updated directly (SHA256
`9B6816F9FC030FF79E1FBBF4D4BE05ED0D211C27527CFE6E7001A5FDAEAABC8E`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Primary
confusion moves, confusion-curing items, freeze and broader volatile
status handling remain gaps. Detailed original-C comparison is deferred; new behavior
is UNVERIFIED beyond source inspection and basic checks.

## Source Sing and sleep-turn batch

COMPLETED/PACKAGED 23:24 local. Ported Sing's source status script ordering for the
bounded battle executor. It consumes PP before target checks, rejects Substitute,
existing primary status, Insomnia, Vital Spirit, active Uproar and Soundproof before
the accuracy draw where the source does, then assigns the source `(Random() & 3) + 2`
sleep counter. Sleeping battlers decrement before their action, spend no PP while still
asleep, and wake and act when the counter reaches zero. Early Bird subtracts two turns;
Uproar wakes non-Soundproof battlers and clears Nightmare state.

Focused checks cover exact two-turn generation, ordinary and Early Bird decrement,
Insomnia/Soundproof prevention, Uproar wakeup, Nightmare cleanup and an integrated
Sing turn where the sleeping target retains PP and last-move state. Serialized
`tools/build.ps1 -Release` exited 0: core startup, all 68 library tests, packaged
`--check`, `--field-smoke` and `--scene-smoke` passed. Output executable updated
directly (SHA256
`EEA6FAB55FB3DA96CA33E1ECA0C4A6CF786B1C7351B4829098360FC3C8BD552A`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Safeguard side
state, Sleep Talk, Snore, freeze, confusion and broader status/ability handling remain
gaps. Detailed original-C comparison is deferred; new behavior is UNVERIFIED beyond
source inspection and basic checks.

## Source Thunder Shock paralysis batch

COMPLETED/PACKAGED 22:54 local. Ported Thunder Shock's damaging-move paralysis path
through the generalized secondary-effect handler. It uses the imported 10% chance and
FireRed's inclusive comparison, applies Serene Grace, and preserves Limber, Shield
Dust, existing-status and no-effect rejection after the RNG draw. Paralysis quarters
the staged Speed result and now performs the source `Random() % 4 == 0` action check
before accuracy, the attack announcement and PP reduction. A stopped battler consumes
no PP and does not replace its last-used move.

The standalone enemy-response path now also reaches end-turn status damage after a
miss or full paralysis, matching the normal turn boundary used after failed catches
and escapes. Focused checks cover secondary-chance application, Limber, Speed,
full-paralysis RNG, message, PP and last-move behavior. Serialized
`tools/build.ps1 -Release` exited 0: core startup, all 67 library tests, packaged
`--check`, `--field-smoke` and `--scene-smoke` passed. Output executable updated
directly (SHA256
`80A7EFDD17D074BCA1FAD9C8AAA1E34F37CDD94D9DAA2059F54DEA07A2A46ABD`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Sleep, freeze,
confusion, Safeguard/substitute and broader status/ability handling remain gaps.
Detailed original-C comparison is deferred; new behavior is UNVERIFIED beyond source
inspection and basic checks.

## Source Ember burn batch

COMPLETED/PACKAGED 22:26 local. Ported the damaging-move burn path used by Ember.
Battle moves now read their exact `secondaryEffectChance` from the existing core
import; `Cmd_seteffectwithchance` preserves FireRed's `Random() % 100 <= chance` draw
and rejection order. Burn is blocked by existing primary status, Fire typing, Water
Veil and Shield Dust. Serene Grace doubles the chance. Integrated and fixture
Pokémon now retain their source ability, selected from personality when applicable.

Burn deals max HP / 8 at end of turn and halves physical damage before the source
final +2; Guts bypasses that penalty and special damage is unchanged. Poison Sting
uses the same generalized source-ordered secondary routine. Focused checks cover the
chance draw, status, Fire/Water Veil prevention, residual damage, physical penalty,
special branch and Guts exception. Serialized `tools/build.ps1 -Release` exited 0:
core startup, all 66 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`39D1AE173E39DB05FC3F846C127C6EFAD38FD1D71126AD5B70DEA295EDD72E07`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Paralysis,
sleep, freeze, confusion, Safeguard/substitute and most status/ability interactions
remain gaps. Detailed original-C comparison is deferred; new behavior is UNVERIFIED
beyond source inspection and basic checks.

## Generation III STAB and type-effectiveness batch

COMPLETED/PACKAGED 20:57 local. Ported `Cmd_typecalc` for the bounded battle engine:
same-type attack bonus is applied with the source 15/10 integer order, followed by
the complete FireRed `gTypeEffectiveness` chart. Distinct dual types are processed in
order, matching source rounding, cancellation and immunity behavior. Actual attacks
and the AI `if_can_faint` estimate now use typed damage; battle output reports immune,
resisted and super-effective results. `CalculateBaseDamage` stays separate so the
existing original-C fixtures continue to verify that source stage unchanged.

Focused checks cover STAB plus super effectiveness, resistance, duplicate types,
dual-type cancellation, immunity with zero damage and the result message. Serialized
`tools/build.ps1 -Release` exited 0: core startup, all 65 library tests, packaged
`--check`, `--field-smoke` and `--scene-smoke` passed. Output executable updated
directly (SHA256
`C56EA71FB814CECC3CA2426C6156965ABB91D847C045A1BAFD77362E30531A0B`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Levitate,
Wonder Guard, Foresight, dynamic move/type changes, Struggle, burn, items, badges,
weather, screens and doubles remain damage gaps. Detailed original-C comparison is
deferred; new behavior is UNVERIFIED beyond source inspection and basic checks.

## Generation III physical/special damage batch

COMPLETED/PACKAGED 20:23 local. `CalculateBaseDamage` now selects offensive and
defensive stats from the source move type: Normal through Steel use Attack/Defense,
while Fire through Dark use Special Attack/Special Defense. Battle stat storage now
includes the source Special Attack and Special Defense stages, and critical hits apply
the same source rules for ignoring harmful attacker stages and beneficial defender
stages on either branch. Existing exact move types come from the read-only core import;
no generated data changed.

A focused check gives physical and special moves deliberately opposed stat spreads,
then verifies special-stage and critical behavior. The original C physical damage
fixtures remain unchanged. Serialized `tools/build.ps1` exited 0: core startup, all 63
library tests, packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output
executable updated directly (SHA256
`FB4BC45DA99431623D049F81F4AFE03DC3BD6A7F1CC3A954D3D46660DE7B4C64`); no pending
executable. Native music remains 0 and installed C++ game is untouched. STAB, type
effectiveness, burn, items, badges, abilities, weather, screens and doubles remain
damage gaps. Detailed original-C comparison is deferred; new behavior is UNVERIFIED
beyond source inspection and basic checks.

## Source Sand Attack and accuracy-stage batch

COMPLETED/PACKAGED 19:53 local. Ported Sand Attack's actual Accuracy-stage change and
the source 13-entry accuracy/evasion fraction table. Accuracy and Evasion now occupy
their source-neutral stage 6 positions, combined stages clamp to 0-12, and the adjusted
accuracy is applied before the existing critical calculation with the original integer
order and RNG draw. Sand Attack respects the minimum stage instead of merely printing
a message.

The bounded AI now evaluates the `if_status`/`if_not_status` variants for persistent,
status2 and status3 fields, plus source Accuracy/Evasion stage queries needed by
Pidgey's scoring path. Volatile fields begin clear and remain explicit state for later
effects. A focused check forces a Route 1 Pidgey to use Sand Attack and verifies the
stage change and a resulting 75% hit threshold. Serialized `tools/build.ps1` exited 0:
core startup, all 62 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`90D30D53F2EEB9D28EEF95BA87B3D32E3C174E05BA14FA31F8F29AAC4301C50F`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Foresight,
accuracy/evasion items and abilities, weather modifiers, Protect and other stage moves
remain gaps. Detailed original-C comparison is deferred; new behavior is UNVERIFIED
beyond source inspection and basic checks.

## Source last-move/type AI batch

COMPLETED/PACKAGED 19:25 local. Completed the source AI path that blocked opponent
Harden without changing Gemini-owned importers or generated files. The battle adapter
now reads exact species and move types from the existing source-backed `core.json`,
tracks each battler's last used move (including misses), and executes
`get_last_used_move`, move power/type lookup, user/target type lookup and
`if_in_bytes`/`if_not_in_bytes` source tables. `MOVE_NONE` retains source power/type
zero behavior.

The Harden check now runs two opponent turns across all 256 low-byte deterministic
seeds, exercising the prior last-move/type scoring branches as well as the Defense
increase. Serialized `tools/build.ps1` exited 0: core startup, all 61 library tests,
packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output executable
updated directly (SHA256
`D5593918B1D3AABA9A9FAE15F6EE0B0709F0096CAFE63F84A1CA80A73D10410C`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Full type
effectiveness, physical/special damage separation, switching history and many other AI
commands remain gaps. Detailed original-C comparison is deferred; new behavior is
UNVERIFIED beyond source inspection and basic checks.

## Source Harden and no-accuracy-script batch

COMPLETED/PACKAGED 18:55 local. Added the source `EFFECT_DEFENSE_UP` path used by
Harden. It raises the user's Defense stage by one up to stage 12, emits the source
rise/cap results, and immediately affects the existing staged physical damage
calculation. Moves whose imported accuracy is zero now follow their source scripts by
skipping `Cmd_accuracycheck` and consuming no accuracy RNG draw. The bounded AI also
supports the source equal/not-equal HP percentage branches encountered by stat-up
scoring.

A focused library check covers the zero-accuracy RNG behavior, one-stage increase,
maximum-stage result and reduced physical damage. Serialized `tools/build.ps1` exited
0: core startup, all 61 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`D58F4AC3D6C4B47CC9868B0A6398BFDD72B0BE07D98334798E7F639660DC5196`); no pending
executable. Native music remains 0 and installed C++ game is untouched. The following
last-move/type AI batch closes the opponent-Harden selection gap. Other stat-up effects
and special damage remain gaps. Detailed original-C comparison is deferred; new
behavior is UNVERIFIED beyond source inspection and basic checks.

## Source String Shot and speed-order batch

COMPLETED/PACKAGED 18:24 local. Extended the bounded battle interpreter for Bug
Catcher Sammy's remaining source move. `MOVE_STRING_SHOT` now performs its normal
accuracy check, lowers the target's Speed stage by one, respects the minimum stage,
and reports the source-style result instead of failing as an unsupported effect.
Turn order now uses the staged Speed stat and the source paralysis quartering after
move priority. The AI's `if_target_faster` command uses the same calculation and
preserves the equal-Speed RNG draw made by `GetWhoStrikesFirst`.

A focused library check forces String Shot through the trainer AI and verifies its
stage change, changed action order and paralysis divisor. Serialized
`tools/build.ps1` exited 0: core startup, all 60 library tests, packaged `--check`,
`--field-smoke` and `--scene-smoke` passed. Output executable updated directly
(SHA256 `07A5C7488C506B8F659E3039BD44604DAB5F26529549C19289EC72BBF7E62336`);
no pending executable. Native music remains 0 and installed C++ game is untouched.
Badge, held-item, weather and ability Speed modifiers remain gaps. Detailed
original-C comparison is deferred; new behavior is UNVERIFIED beyond source
inspection and basic checks.

## Source Poison Sting and end-turn poison batch

COMPLETED/PACKAGED 17:58 local. Extended the bounded battle interpreter for the poison
path used by Bug Catcher Sammy's source Weedle. `MOVE_POISON_STING` now consumes the
source secondary-effect RNG draw after a hit, preserves FireRed's `Random() % 100 <= 30`
comparison, and applies regular poison only to a living, status-free target. Integrated
party Pokémon derive Poison/Steel/Immunity protection from imported core species and
ability data. Rejected effects still consume the already-drawn chance as the source
script does.

End-of-turn processing now applies regular poison at max HP / 8 (minimum one) and toxic
poison at max HP / 16 times the capped 1-15 counter, in player/opponent order. Residual
faints use the same win/loss and single-participant experience/EV path as attack KOs,
and persistent status synchronizes back through the existing world-party adapter. This
is deliberately bounded to Poison Sting plus poison residuals; other status-causing
moves, Synchronize/Shield Dust/Safeguard/substitute interactions and broad ability
handling remain explicit gaps.

A focused library check covers chance-draw ordering, successful poison, Poison-type
immunity, regular residual damage, toxic counter/damage and residual-KO experience.
Serialized `tools/build.ps1` exited 0: core startup, all 59 library tests, packaged
`--check`, `--field-smoke` and `--scene-smoke` passed. Output executable updated
directly (SHA256
`CFF3E9E9159402EC15CFB4034A5B288BCEA3BBF43718256F13C06EA4B13D45E5`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Detailed
original-C comparison is deferred; new behavior is UNVERIFIED beyond source inspection
and basic checks.

## Source field-poison step batch

COMPLETED/PACKAGED 17:25 local. Ported
`field_control_avatar.c:UpdatePoisonStepCounter` and
`field_poison.c:DoPoisonFieldEffect/FaintFromFieldPoison` into persistent native
state. Ordinary, non-forced field steps advance source var `VAR_POISON_STEP_COUNTER`
(0x4022) modulo five. On each wrap, regular or toxic poison removes one HP from every
affected party Pokémon. A poison faint clears status and applies the source
`FRIENDSHIP_EVENT_FAINT_OUTSIDE_BATTLE` tier loss (-5 below 200 friendship, otherwise
-10). Source-named Secret Base maps are excluded.

Any poison faint preempts same-step Repel and wild-encounter processing as in
`TryStartStepBasedScript`. Partial-party fainting reports the event and continues. If
the whole party faints, the adapter uses the already ported source whiteout money,
healing and checkpoint warp. Exact nickname substitution, the poison palette flash and
its sound remain presentation gaps; no movement or battle redesign was added.

A focused library check covers the five-step counter, both poison bits, simultaneous
party damage, friendship tiers, status cleanup and Secret Base exclusion. Packaged
`--check` forces a fatal field-poison step and verifies counter wrap, friendship, money,
healing, dialogue and Viridian checkpoint recovery. Serialized `tools/build.ps1`
exited 0: core startup, all 58 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`DC7068ECEFD630BA1677B0D273DAB939BB8B29C032293912BA65B4A096AB688F`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Happiness,
Vs Seeker, egg and Safari step counters remain gaps. Detailed original-C comparison
is deferred; new behavior is UNVERIFIED beyond source inspection and basic checks.

## Source trainer-fixture, Pokédex and prize batch

COMPLETED/PACKAGED 13:31 local. Audited every previously accepted trainer fixture
against `src/data/trainer_parties.h` and removed the shortened multi-Pokémon fights
that could falsely set a trainer's defeated flag. The bounded single-mon engine now
accepts only the complete Oak-lab rival parties (trainer IDs 326-328, level 5) and Bug
Catcher Sammy (104, level 9 Weedle); Rick, Doug, Route 22 rivals, Camper Liam, Brock and
all other incomplete parties fail explicitly until switching and full trainer-party
imports exist.

Trainer opponents now set the persistent Pokédex seen bit when their battle begins,
following `battle_main.c`'s trainer-side `HandleSetPokedexFlag`. Wins add the exact
`Cmd_getmoneyreward` prize for the supported party/class combinations: ¥80 for the
Oak-lab rivals and ¥108 for Sammy. A participating Amulet Coin holder doubles that
reward through imported `HOLD_EFFECT_DOUBLE_PRIZE`; the existing source money cap and
overflow behavior remains in `State::add_money`. Loss handling remains unchanged.

A focused unit check covers held-item prize metadata. Packaged `--check` now verifies
multi-mon rejection, trainer seen registration, Sammy's doubled ¥216 reward, result and
defeated flag. Serialized `tools/build.ps1` exited 0: core startup, all 57 library tests,
packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output executable updated
directly (SHA256
`C55943C1609E78FBF862A1A7F36D6052BFB237E460999D76E68DBDF67F45265B`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Trainer-party
imports/switching, prize dialogue and broad battle species/moves remain gaps. Detailed
original-C comparison is deferred; new behavior is UNVERIFIED beyond source inspection
and basic checks.

## Source special-ball mechanics batch

COMPLETED/PACKAGED 12:54 local. Extended the regular single-wild-battle
`battle_script_commands.c:Cmd_handleballthrow` port with Net, Dive, Nest, Repeat,
Timer, Luxury and Premier Ball multipliers. Net Ball reads the opponent's imported
source types, Repeat Ball reads the persistent Pokédex caught bit, Nest Ball uses the
opponent level and Timer Ball uses the capped source turn counter. The Timer Ball's
late `u8` addition wrap is retained before its x4 cap. FireRed has no underwater map
type, so integrated Dive Ball context correctly remains x1.

Walking battles capture their catch context before marking a generated opponent seen,
so Repeat Ball distinguishes seen-only from previously caught. Successful special-ball
captures retain the ball item in party/PC metadata. Safari Ball remains explicitly
rejected because its separate Safari catch-factor initialization is not ported. The
current battle UI still throws only a Poke Ball; original bag selection remains a gap.

Focused checks cover imported Water/Bug types, every special multiplier, Nest/Timer
boundaries, the Timer wrap quirk, Safari rejection and retained Net Ball metadata.
Packaged `--check` verifies the field adapter supplies uncaught context and later Repeat
Ball caught context. Serialized `tools/build.ps1` exited 0: core startup, all 56 library
tests, packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output executable
updated directly (SHA256
`22E60A9F85C3D44979178C619332736DF80665D35F1936125272A679FD121677`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Detailed
original-C comparison is deferred; new behavior is UNVERIFIED beyond source inspection
and basic checks.

## Trainer battle field-state batch

COMPLETED/PACKAGED 12:27 local. Corrected the existing bounded trainer-battle field
bridge against `battle_setup.c:CB2_EndTrainerBattle` and `battle_main.c` battle-result
handoff. Trainer scripts now preserve their original battle mode as well as rival flags.
Supported trainer fixtures begin from the current persistent RNG state and return their
final state/exact call count, select the first usable party Pokemon, and synchronize it
back on either outcome. Normal battles expose source outcome 1/2; early-rival battles
expose FALSE/TRUE. Defeated flags are set on wins and on the tutorial early-rival loss
that continues, but no longer on ordinary losses.

The source `RIVAL_BATTLE_HEAL_AFTER` bit is now bit 0. Its tutorial loss heals and
continues the field script; ordinary and unhealed early-rival losses use the already
ported money/heal/checkpoint whiteout and stop the interrupted script. Unknown trainer
teams, an empty/fully fainted party, and Pokemon adapter failures now report explicit
gaps instead of fabricating Squirtle or Charmander battles. Integrated trainer and
wild battles cannot be restarted into the isolated rival fixture.

The new VM check confirms the Oak-lab early-rival mode/flags. Packaged `--check` forces
that loss path and verifies healing, trainer flag/result, unchanged map, script release,
and exact RNG continuity. Serialized `tools/build.ps1` exited 0: core startup, all 54
library tests, packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output
executable updated directly (SHA256
`844FD1F0F653E4C0B08E3E2F5B3CC5CA50FB4C652FBC4A92692AABD995CDEAEA`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Complete
trainer-party imports, multi-Pokemon switching, prize money, broad species/moves and
trainer Pokédex registration remain gaps. Detailed original-C comparison is deferred;
new behavior is UNVERIFIED beyond source inspection and basic checks.

## Persistent Pokédex state batch

COMPLETED/PACKAGED 11:54 local. Added semantic native Pokédex persistence with the
source's 52-byte seen/owned capacity, National Dex enable state and first-seen
Unown/Spinda personalities. Ported all 411 entries of
`pokemon.c:sSpeciesToNationalPokedexNum`: Kanto/Johto identity ordering, 25 legacy
Unown slots and FireRed's non-linear Hoenn internal order.

Walking wild battles mark the generated opponent seen when battle begins. Successful
captures mark caught after party/PC acceptance, and script `givemon` does the same for
both destinations. `GetPokedexCount` now writes Kanto or National seen/caught totals to
special vars 0x8005/0x8006 and returns National Dex status; `SetSeenMon`,
`EnableNationalPokedex` and `IsNationalPokedexEnabled` now update/read their source
markers. Older native saves gain an empty Dex.

Focused checks cover mapping boundaries/non-linear entries, seen/caught semantics,
special personalities, Kanto/National counts, all three starter gifts and National Dex
marker redundancy. Packaged `--check` verifies a wild encounter starts seen-only and
becomes caught after PC transfer. Serialized `tools/build.ps1` exited 0: core startup,
all 53 library tests, packaged `--check`, `--field-smoke` and `--scene-smoke` passed.
Output executable updated directly (SHA256
`8AEFD870894D38F4B6B4BC2A7B86E7FA88FDD11EC41C189A4E5837751D6C3E87`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Pokédex UI,
trainer-battle/evolution/trade registration and byte-compatible save-block anticheat
replicas remain gaps. Detailed original-C comparison is deferred; new behavior is
UNVERIFIED beyond source inspection and basic checks.

## Source whiteout and heal-location batch

COMPLETED/PACKAGED 08:54 local. Integrated wild-battle losses now port the regular
`overworld.c:DoWhiteOut` state changes: money loss uses highest party level times four,
the source badge-count multiplier table and a current-money cap, then party HP/status/PP
are fully restored. The field host consumes the checkpoint maintained by the already
ported `setrespawn` command and uses `heal_location.c` recovery coordinates for imported
Pallet, Viridian, Pewter, Cerulean, Lavender, Vermilion, Celadon, Route 4 and Route 10
destinations. Unimported heal maps report an explicit source gap. Older native saves now
default their absent checkpoint field to Pallet instead of invalid location zero.

One focused library check covers badges, highest level, the money cap and healing.
Packaged `--check` additionally forces a walking-battle loss and verifies Viridian
Center position, money and party recovery. Serialized `tools/build.ps1` exited 0:
core startup, all 52 library tests, packaged `--check`, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`4883AA8C690FE2B958C0183D3DEE90DFA8ABCAB94D1F96C2DC8485EB7A4E76A8`); no pending
executable. Native music remains 0 and installed C++ game is untouched. The 120-frame
delay, recovery NPC cutscene/scripts, Elite Four reset, quest-log/overworld mode resets
and unimported heal maps remain gaps. Detailed original-C comparison is deferred; new
behavior is UNVERIFIED beyond source inspection and basic checks.

## Wild battle global RNG continuity batch

COMPLETED/PACKAGED 08:24 local. Walking wild battles now continue the persistent
source `random.c` stream: battle initialization copies the current world RNG state
after encounter selection and wild creation without consuming a fabricated `next32`
seed, then battle resolution returns the final state and exact battle call count to
the world stream. Flee failures, attacks and catch shakes therefore advance the same
saved RNG used by later field mechanics.

Packaged `--check` asserts battle/world start-state identity and exact post-capture
state/call continuity. Serialized `tools/build.ps1` exited 0: core startup, all 51
library tests, packaged `--check`, `--field-smoke` and `--scene-smoke` passed. Output
executable updated directly (SHA256
`9DDB1AA364210862ADCF79D395ADFC4A9D364C616314EC6F015372F930C61A8B`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Trainer-battle
world integration and original per-frame background RNG remain explicit gaps. Detailed
original-C comparison remains deferred; new behavior is UNVERIFIED beyond source
inspection and basic checks.

## PC storage and full-party capture batch

COMPLETED/PACKAGED 07:56 local. Added semantic native `BoxPokemon` storage with the
source's 14 boxes x 30 slots, current box, reset names and four default wallpaper
indices. `State::give_mon` ports the regular `pokemon.c:GiveMonToPlayer` and
`SendMonToPC` flow: player OT assignment, current-box start, circular box scan,
first-empty-slot selection, `MonRestorePP`, destination special vars, persisted
`VAR_PC_BOX_TO_SEND_MON`, and box-full-message flag clearing when the box changes.
Field `givemon` now returns source result 0/1/2 for party/PC/full.

Full-party catches are transferred into native PC storage and report the destination
box after battle. The ball used to catch a Pokemon is retained through the battle/core
adapter. Ball use is rejected without consuming an item when both party and all boxes
are full, matching the source bag guard. Existing native saves remain compatible via
default empty storage and default Poke Ball metadata. PC menus, withdrawal/deposit,
release, full encrypted BoxPokemon metadata, dex flags and exact message scripts remain
explicit gaps.

Serialized `tools/build.ps1` exited 0: core startup/storage persistence, all 51 library
tests, packaged `--check` with full-party capture to Box 1, `--field-smoke` and
`--scene-smoke` passed. Output executable updated directly (SHA256
`0B53F64BF2C13459B63FB68F93192F139181C656FA65083877B21DC3311644F2`); no pending
executable. Native music remains 0 and installed C++ game is untouched. Detailed
original-C comparison remains deferred; new behavior is UNVERIFIED beyond source
inspection and basic checks.

## Wild flee/catch mechanics batch

COMPLETED/PACKAGED 07:29 local. Ported the regular wild-battle branches of
`battle_main.c:TryRunFromBattle` and `battle_script_commands.c:Cmd_handleballthrow`.
Fleeing now uses source speed division, wrapping u8 retry odds, 16-bit RNG low byte,
Run Away and Smoke Ball. Basic Ultra/Great/Poke Ball catch multipliers, HP/status
odds, nested integer square roots, up to four shake draws and the Master Ball's
post-calculation force-success quirk are retained. Status and always-run metadata
are sourced from the native party adapter.

The wild battle UI throws one real Poke Ball with C, removes it from the persisted
native bag, gives the opponent a turn on failure, and reports an empty ball pocket.
Unresolved wild battles reject Escape and fixture restart, preventing cancellation
or replacement of the pending world battle. Successful capture retains the generated
Pokemon and existing field bridge adds it to the party; a full party still needs PC
routing. Only the Poke Ball UI path is exposed; other ball contexts and selection UI
remain gaps, as do shared world/battle RNG and faithful blackout.

Serialized `tools/build.ps1` exited 0: core startup, all 49 library tests, packaged
`--check`, `--field-smoke` and `--scene-smoke` passed. Focused battle checks include
run-attempt/RNG ordering, guaranteed basic catch and Master Ball RNG consumption.
Output executable updated directly (SHA256
`95EAD9F8C820B937DB3F265140C81C35B84350D23B68EFF14963804B1A0B21C4`); no pending
executable. Native output music set to 0. Installed C++ game untouched. Detailed
original-C comparison remains deferred and this new behavior is UNVERIFIED beyond
source inspection and basic checks.

## Active handoff batch

COMPLETED/PACKAGED 06:54 local: reviewed Gemini's stable 137-map Celadon handoff
(74,385 cells, 143 textures, 571 actors) and integrated the ROM-verified per-cell
encounter_types. Serialized build.ps1 exited 0: core startup, all 48 library tests,
packaged --check (including the actual walking/Repel/battle bridge), --field-smoke
and --scene-smoke passed. Updated output/FireRed Native 3D Prototype.exe directly;
no pending executable was needed. Installed C++ game remained untouched and QA silent.

Regular land walking now calls Data::land_step using original terrain attributes,
private rate RNG, main 16-bit draws, cooldown, modifiers and persisted Repel count.
Repel expiry starts EventScript_RepelWoreOff before the encounter test. Initial
placement/map entry do not count as steps; map entry and battle start reset immunity.
Wild creation uses CreateMonWithNature-style personality rejection and random IVs.
Battle/capture results sync the selected conscious party slot, capture preserves the
generated mon, and saving remains locked until resolution. Unsupported species/moves
report a source gap; no fallback species/player mon is fabricated.

Validation added: cave land encounter attributes independent of grass behavior,
native modifier sourcing, random-IV identity/RNG consumption, Repel script completion,
bad world attribute rejection, and headless host walking checks for placement, Repel,
battle start/save lock and missing-species failure. Detailed original-C/frame parity
remains deferred and the documented unsupported systems below remain unchanged.

Accepted Gemini's explicit release of main.rs/field_screen.rs. Codex also reserves
lib.rs for encounter_types map reading/validation. Sent Celadon geography and
per-cell encounter attribute import/ROM verification to the existing Antigravity
chat using Computer Use; live UI confirmed Gemini working. Gemini instructed not
to compile/package; Codex owns combined build after generated-data stability.

Implemented host land_step hookup with private RNG, native modifiers, Repel
expiry script and initial-placement/map-change exclusions. Removed fabricated
wild/player species fallbacks, added native wild creation with source nature/
personality/random-IV flow, retained captured identity and selected party slot.
Walking encounters follow existing field_scripts/battle_slice settings. Private
encounter counters/RNG remain transient and reseeded at host session/reset boundary;
Repel stays in persisted original vars. Mid-battle saves blocked by script lock.
This batch is delivered in the separate native Rust package.

Known gaps: no native egg/cycling/forced-avatar/Union Room/quest-log playback state,
water/roamers/Unown/header overrides; Tower ghost/Safari battle setup explicitly
rejected. Full battle engine/RNG sharing, full-party capture PC routing and blackout
remain incomplete. Experimental free/diagonal movement is not original frame parity.
Source references: wild_encounter.c, pokemon.c:CreateMonWithNature,
field_control_avatar.c:TryStartStepBasedScript, overworld.c map resets,
new_game.c:ResetMenuAndMonGlobals, battle_setup.c:Task_BattleStart.
New behavior remains UNVERIFIED against original C; basic checks required.

## Latest heartbeat

02:20 UTC follow-up: Antigravity access restored. Sent Route 9/10, Rock Tunnel,
Lavender importer assignment and requested field_screen.rs transfer acknowledgment.
Ported Events::update_repel_counter from wild_encounter.c:UpdateRepelCounter;
preserves Union Room/playback exclusions and reports the one-time wore-off script request.
44 library tests passed. Repel counter is not yet hooked into walking.
No packaging during Gemini's fresh import batch; next combined package after stable handoff.

Gemini reports 93 maps complete, trash-can RNG corrected, and main.rs ownership offered
to Codex. Accepted main.rs handoff, but actual encounter trigger is in field_screen.rs.
Request its ownership before integration; do not edit until acknowledged.
Computer Use activation timed out awaiting app approval; next Gemini assignment was
not delivered. Proposed Route 9/10, Rock Tunnel and Lavender importer batch is on task board.

Added Data::land_step regular-land source API: cooldown, disabled flag, terrain-change
60% gate, private rate RNG, weighted slot/level, Repel check using first eligible
party member, failure buff and success reset. Source wild_encounter.c functions
TryStandardWildEncounter/StandardWildEncounter/TryGenerateWildMon/IsWildLevelAllowedByRepel.
43 library tests passed. Not yet integrated into walking; roamers, Unown generation
and map header overrides explicitly unsupported. Packaging complete: build.ps1 exited
0 including core startup, all 43 library tests, --check, --field-smoke and --scene-smoke.
Native output updated; installed C++ untouched. Only nonfatal cache warnings.

Coordination started with the existing Antigravity chat; Gemini received and read the task board.
Reserved encounter.rs, battle.rs and core.rs against overlapping Gemini edits.
Current edits are confined to encounter.rs and coordination documentation.

## Encounter batch

Source: reference/pokefirered/src/wild_encounter.c, ChooseWildMonLevel,
DoWildEncounterRateTest, ApplyFluteEncounterRateMod, ApplyCleanseTagEncounterRateMod.
Corrected fixed-level RNG consumption and reversed level bounds.
Added source-order integer encounter rate modifiers (bike, accumulated buff,
flutes, Cleanse Tag, Stench/Illuminate, ability bypass and cap).
All 40 library checks passed (`cargo test --lib --no-default-features --locked`).
Only nonfatal incremental-cache hard-link/access warnings occurred.
Full source parity has not been claimed.

## Follow-up batch / packaging lock

Packaging complete: Gemini may proceed with the assigned next world batch.
Reviewed Gemini's 62-map status report. Native dev package updated successfully;
build.ps1 exited 0 including core startup, --check, --field-smoke and --scene-smoke.
Installed C++ game untouched.
Added EncounterState private RNG (ISO_RANDOMIZE2), u16 rate buff accumulation/Repel
reset, source cooldown fixed-point arithmetic, and successful-encounter reset primitive.
42 library tests passed after the final u16 buff-width correction. Only nonfatal
incremental-cache warnings remain. Detailed behavior parity comparisons deferred.
These primitives are not yet wired to host walking. Remaining integration needs main.rs ownership.
Review finding sent to Gemini: trash-can special must use next16(), not next32(),
because original Random() consumes one 16-bit draw.

Still missing from host adapter: dedicated encounter RNG, cooldown state,
repel/terrain gates, lead abilities and modifiers wired from native state.
Next Codex batch should wire these after requesting main.rs ownership from Gemini.

Gemini owns the current world expansion; read its status before integration/build.
The active 30-minute thread heartbeat continues useful batches within account/tool limits.
