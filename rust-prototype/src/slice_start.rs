//! Playable-slice quick start (Settings Y / `quick_start`, default on).
//! With an empty party the player picks a starter (1/2/3); the save is then put in the
//! source post-Pokedex state so Oak's escort, the lab scenes and the parcel errand are
//! skipped and Pallet Town / Route 1 play freely: wild grass encounters, battles, catching.
use super::*;
use firered_native_prototype::core::{OtId, Pokemon};

pub(super) const STARTERS: [&str; 3] = ["Bulbasaur", "Charmander", "Squirtle"];

pub(super) fn needs_starter(p: &Preferences, session: &persistence::Session) -> bool {
    p.quick_start && session.state.party.is_empty()
}

pub(super) fn update(
    keys: Res<ButtonInput<KeyCode>>,
    p: Res<Preferences>,
    s: Res<Simulation>,
    battle: Res<battle_screen::Screen>,
    mut session: ResMut<persistence::Session>,
) {
    if !needs_starter(&p, &session) || p.menu || battle.active || s.game.dialogue.is_some() {
        return;
    }
    let pick = [KeyCode::Digit1, KeyCode::Digit2, KeyCode::Digit3]
        .iter()
        .position(|k| keys.just_pressed(*k));
    if let Some(i) = pick {
        if let Err(e) = give(&mut session, i) {
            session.error = format!("Quick start failed: {e}");
        }
    }
}

/// Source equivalents: EventScript_ChoseStarter (givemon level 5), the dex scene in
/// PalletTown_ProfessorOaksLab_EventScript_ProfOak (5 Poke Balls, FLAG_SYS_POKEDEX_GET)
/// and the scene vars those scripts leave behind.
fn give(session: &mut persistence::Session, choice: usize) -> Result<(), String> {
    let persistence::Session { data, state, .. } = session;
    let id = |name: &str| data.ids.get(name).copied().ok_or(format!("Missing {name}"));
    let species = id(&format!("SPECIES_{}", STARTERS[choice].to_uppercase()))?;
    let mon = Pokemon::create(data, &mut state.rng, species, 5, 32, None, OtId::Player, state.trainer_id)?;
    let personality = mon.personality;
    state.give_mon(data, mon)?;
    state.pokedex.set_seen(species, personality)?;
    state.pokedex.set_caught(species, personality)?;
    state.events.flag_set(0x828); // FLAG_SYS_POKEMON_GET
    state.events.flag_set(0x829); // FLAG_SYS_POKEDEX_GET
    for (var, value) in [
        (0x4050, 1), // VAR_MAP_SCENE_PALLET_TOWN_OAK: escort done
        (0x4055, 6), // VAR_MAP_SCENE_PALLET_TOWN_PROFESSOR_OAKS_LAB: after the Pokedex
        (0x4057, 2), // VAR_MAP_SCENE_VIRIDIAN_CITY_MART: parcel delivered
        (0x4051, 1), // VAR_MAP_SCENE_VIRIDIAN_CITY_OLD_MAN
        (0x4058, 1), // VAR_MAP_SCENE_PALLET_TOWN_RIVALS_HOUSE
        (0x4054, 1), // VAR_MAP_SCENE_ROUTE22
    ] {
        state.events.var_set(var, value)?;
    }
    // Lab objects: the player's and the rival's balls, the rival and both Pokedexes are gone.
    let rival_ball = [42, 40, 41][choice]; // rival takes the starter with the type advantage
    for flag in [[40, 42, 41][choice], rival_ball, 45, 58] {
        state.events.flag_set(flag);
    }
    let ball = id("ITEM_POKE_BALL")?;
    let potion = id("ITEM_POTION")?;
    state.bag.add(data, &mut state.events, ball, 5);
    state.bag.add(data, &mut state.events, potion, 3);
    Ok(())
}

/// QA: `--slice-auto` picks Charmander, then paces east/west in the grass at the current
/// spot until a wild battle starts; with --capture the screenshot is taken in battle.
pub(super) fn auto(
    mut s: ResMut<Simulation>,
    battle: Res<battle_screen::Screen>,
    mut session: ResMut<persistence::Session>,
    mut capture: ResMut<Capture>,
    mut frame: Local<u32>,
    mut seen_battle: Local<bool>,
) {
    if !std::env::args().any(|a| a == "--slice-auto") {
        return;
    }
    if session.state.party.is_empty() {
        let _ = give(&mut session, 1);
    }
    *frame += 1;
    if battle.active {
        if !*seen_battle {
            *seen_battle = true;
            eprintln!("[slice-auto] wild battle after {} frames", *frame);
            capture.frame = capture.frame.min(200);
        }
        return;
    }
    if *frame > 30 {
        let dir = if (*frame / 45) % 2 == 0 { 1. } else { -1. };
        s.free_vector = [dir, 0.];
        s.controls.direction = Some(firered_native_prototype::Cell(dir as i32, 0));
    }
    if !*seen_battle && *frame < 4000 {
        capture.frame = capture.frame.min(10); // hold the screenshot until the battle starts
    }
}
