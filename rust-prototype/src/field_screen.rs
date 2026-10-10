//! Presentation/interaction adapter for the original field VM. No guest CPU.
use super::{persistence::Session, Preferences, Simulation};
use bevy::prelude::*;
use firered_native_prototype::{
    actors::{Actors, Data as ActorData},
    core::State,
    field::{Data, Host, Step, Vm},
};

/// heal_location.c:sWhiteoutRespawnHealCenterMapIdxs and
/// SetWhiteoutRespawnWarpAndHealerNpc. Unsupported locations stay explicit until
/// their source maps are imported.
fn whiteout_respawn(id: u16) -> Result<(&'static str, firered_native_prototype::Cell), String> {
    use firered_native_prototype::Cell;
    match id {
        1 => Ok(("home", Cell(8, 5))),
        2 => Ok(("viridian_center", Cell(7, 4))),
        3 => Ok(("pewter_center", Cell(7, 4))),
        4 => Ok(("cerulean_center", Cell(7, 4))),
        5 => Ok(("lavender_center", Cell(7, 4))),
        6 => Ok(("vermilion_center", Cell(7, 4))),
        7 => Ok(("celadon_center", Cell(7, 4))),
        12 => Ok(("route4_center", Cell(7, 4))),
        13 => Ok(("route10_center", Cell(7, 4))),
        _ => Err(format!("Whiteout heal location {id} is not imported")),
    }
}

#[derive(Clone, Copy)]
struct TrainerFixture {
    enemy_index: usize,
    enemy_level: u16,
    species: &'static str,
    prize: u32,
}

/// Bounded trainer fixtures whose complete source teams fit the current
/// single-mon battle slice. Multi-mon trainers stay explicit instead of being
/// shortened into battles that can incorrectly set their defeat flags.
fn trainer_fixture(trainer: u16) -> Result<TrainerFixture, String> {
    match trainer {
        326 => Ok(TrainerFixture {
            enemy_index: 1,
            enemy_level: 5,
            species: "SPECIES_SQUIRTLE",
            prize: 80,
        }),
        327 => Ok(TrainerFixture {
            enemy_index: 2,
            enemy_level: 5,
            species: "SPECIES_BULBASAUR",
            prize: 80,
        }),
        328 => Ok(TrainerFixture {
            enemy_index: 0,
            enemy_level: 5,
            species: "SPECIES_CHARMANDER",
            prize: 80,
        }),
        104 => Ok(TrainerFixture {
            enemy_index: 8,
            enemy_level: 9,
            species: "SPECIES_WEEDLE",
            prize: 108,
        }),
        _ => Err(format!(
            "Trainer {trainer} complete source party is not imported"
        )),
    }
}

#[derive(Resource)]
pub(super) struct Field {
    data: Data,
    vm: Option<Vm>,
    before: Option<State>,
    name: String,
    waiting: Option<Host>,
    remaining: f64,
    message_ack: bool,
    comparison: u8,
    pub actors: Actors,
    actor_data: ActorData,
    fixture: Option<firered_native_prototype::World>,
    entry: std::collections::VecDeque<u8>,
    blocked: std::collections::HashSet<String>,
    coord_cell: Option<(String, firered_native_prototype::Cell)>,
    selected: u16,
    before_game: Option<firered_native_prototype::Game>,
    before_actors: Option<Actors>,
    actor_time: f64,
    script_label: String,
    pub encounter_data: firered_native_prototype::encounter::Data,
    pub last_step_cell: Option<(String, firered_native_prototype::Cell)>,
    encounters: firered_native_prototype::encounter::EncounterState,
    wild_mon: Option<firered_native_prototype::core::Pokemon>,
    wild_party_slot: usize,
    trainer_party_slot: usize,
}
impl Field {
    pub fn new(seed: u16) -> Self {
        Self {
            data: Data::source(),
            vm: None,
            before: None,
            name: String::new(),
            waiting: None,
            remaining: 0.,
            message_ack: false,
            comparison: 0,
            actors: Actors::default(),
            actor_data: ActorData::source(),
            fixture: None,
            entry: Default::default(),
            blocked: Default::default(),
            coord_cell: None,
            selected: 0,
            before_game: None,
            before_actors: None,
            actor_time: 0.,
            script_label: String::new(),
            encounter_data: firered_native_prototype::encounter::Data::source(),
            last_step_cell: None,
            encounters: firered_native_prototype::encounter::EncounterState::seeded(seed),
            wild_mon: None,
            wild_party_slot: 0,
            trainer_party_slot: 0,
        }
    }
    pub fn active(&self) -> bool {
        self.vm.is_some()
    }
    pub fn reset_world(&mut self, simulation: &mut Simulation) {
        if let Some(world) = self.fixture.take() {
            simulation.world = world;
            simulation.dirty = true;
        }
    }
    fn stop(&mut self) {
        self.vm = None;
        self.waiting = None;
        self.before = None;
        self.before_game = None;
        self.before_actors = None;
        self.actors.clear_tasks();
    }
    fn scene(
        &mut self,
        simulation: &mut Simulation,
        session: &mut Session,
        enabled: bool,
    ) -> Result<(), String> {
        if self.active() {
            return Ok(());
        }
        if !enabled {
            if let Some(fixture) = self.fixture.take() {
                simulation.world = fixture;
                simulation.dirty = true;
                self.actors = Actors::default();
                self.entry.clear();
            }
            return Ok(());
        }
        if self.fixture.is_none() {
            self.fixture = Some(simulation.world.clone());
        }
        if !session.actor_initialized {
            let mut reset = Vm::start(&self.data, "EventScript_ResetAllMapFlags")?;
            if reset.run(&self.data, &session.data, &mut session.state, 1024)? != Step::End {
                return Err("Map initialization did not finish".into());
            }
            session.actor_initialized = true;
        }
        let map = simulation.game.map.clone();
        if self.actors.map != map {
            self.actors.load(
                &self.actor_data,
                &map,
                &session.state,
                session.actors.get(&map).map_or(&[], Vec::as_slice),
            )?;
            self.actors.sync(&mut simulation.world);
            simulation.dirty = true;
            self.entry = std::collections::VecDeque::from([3, 1, 4, 5]);
            self.blocked.clear();
            self.coord_cell = None;
        }
        if simulation.game.motion.is_some() || simulation.game.dialogue.is_some() {
            return Ok(());
        }
        let mut address = None;
        while let Some(tag) = self.entry.pop_front() {
            if let Some(p) = self
                .actor_data
                .table(&self.data, &map, tag, &session.state)?
            {
                address = Some(p);
                break;
            }
        }
        if address.is_none() {
            address = self.actor_data.table(&self.data, &map, 2, &session.state)?;
        }
        let cell = (map.clone(), simulation.game.position);
        if address.is_none() && self.coord_cell.as_ref() != Some(&cell) {
            self.coord_cell = Some(cell);
            for coord in &self.actor_data.maps[&map].coords {
                if coord.at == simulation.game.position
                    && (coord.elevation == 0 || coord.elevation == simulation.game.elevation)
                    && (coord.variable == 0
                        || session.state.events.var_get(coord.variable)? == coord.value)
                {
                    address = Some(self.data.label(&coord.script)?);
                    break;
                }
            }
            if address.is_none() {
                for actor in &self.actors.actors {
                    if !actor.present || actor.object.trainer_type == 0 {
                        continue;
                    }
                    if let Ok(vm) = Vm::start(&self.data, &actor.object.script) {
                        if let Some(trainer_id) = vm.peek_trainer(&self.data) {
                            if session.state.events.flag_get(0x500 + trainer_id) {
                                continue;
                            }
                        }
                    }
                    let mut sight = false;
                    for d in 1..=actor.object.trainer_sight as i32 {
                        let check_pos = firered_native_prototype::Cell(
                            actor.at.0 + actor.facing.0 * d,
                            actor.at.1 + actor.facing.1 * d,
                        );
                        if check_pos == simulation.game.position {
                            sight = true;
                            break;
                        }
                        let step = firered_native_prototype::Cell(
                            actor.at.0 + actor.facing.0 * (d - 1),
                            actor.at.1 + actor.facing.1 * (d - 1),
                        );
                        if firered_native_prototype::collision(
                            &simulation.world,
                            &map,
                            step,
                            actor.object.elevation,
                            actor.facing,
                        ) != firered_native_prototype::Collision::None
                        {
                            break;
                        }
                    }
                    if sight {
                        address = Some(self.data.label(&actor.object.script)?);
                        // Make player face the trainer
                        simulation.game.facing =
                            firered_native_prototype::Cell(-actor.facing.0, -actor.facing.1);
                        break;
                    }
                }
            }
        }
        if let Some(p) = address {
            let label = self
                .data
                .labels
                .iter()
                .find(|(_, a)| **a == p)
                .map(|(s, _)| s.clone())
                .ok_or("Unlabelled scene")?;
            if !self.blocked.contains(&label) && simulation.game.pending_script.is_none() {
                simulation.game.pending_script = Some((label, "Original map scene".into()));
                simulation.game.script_locked = true;
            }
        }
        Ok(())
    }
    fn target(&self, map: Option<[u8; 2]>) -> Result<(), String> {
        if let Some(number) = map {
            if self.actor_data.map_id(number)? != self.actors.map {
                return Err("Off-map object operation not ported".into());
            }
        }
        Ok(())
    }
    fn text(&self, address: u32) -> Result<Vec<String>, String> {
        let mut text = self
            .data
            .text(address)?
            .trim_end_matches('$')
            .replace("{PLAYER}", "RED")
            .replace("{RIVAL}", "BLUE");
        let vm = self.vm.as_ref().ok_or("No current script")?;
        for (i, value) in vm.strings.iter().enumerate() {
            text = text.replace(&format!("{{STR_VAR_{}}}", i + 1), value);
        }
        if text.contains('{') || text.contains('}') {
            return Err("Unported original text placeholder/control".into());
        }
        // The current prototype font lacks the original accented Pokemon glyph.
        text = text.replace("POK\u{e9}MON", "POKEMON");
        // Existing native dialogue presents instant pages; print/fanfare timing is deferred.
        Ok(text
            .split("\\p")
            .map(|page| {
                page.replace("\\n", "\n")
                    .replace("\\l", "\n")
                    .replace("\\\"", "\"")
            })
            .collect())
    }
}

fn start_trainer_battle(
    field: &mut Field,
    battle: &mut super::battle_screen::Screen,
    session: &mut Session,
    trainer: u16,
) -> Result<(), String> {
    let fixture = trainer_fixture(trainer)?;
    let rival_mon = firered_native_prototype::battle::Mon::create_at_level(
        &battle.data,
        fixture.enemy_index,
        fixture.enemy_level,
        [0; 6],
        [0; 6],
        battle.data.rival_personality,
    );
    let party_slot = session
        .state
        .party
        .iter()
        .position(|p| p.hp != 0)
        .ok_or("Trainer battle requires a usable party Pokémon")?;
    let player_mon = firered_native_prototype::battle::Mon::from_pokemon(
        &battle.data,
        &session.data,
        &session.state.party[party_slot],
    )?;
    let species = session.data.id(fixture.species)?;
    session
        .state
        .pokedex
        .set_seen(species, rival_mon.personality)?;
    battle.battle = firered_native_prototype::battle::Battle::from_matchup(
        &battle.data,
        player_mon,
        rival_mon,
        session.state.rng.state,
    );
    // The three Oak-lab rivals use the first-battle tutorial gates. Other
    // source trainer fixtures use the ordinary accuracy and critical paths.
    if matches!(trainer, 326..=328) {
        battle.battle.flags = 0;
    }
    battle.active = true;
    battle.integrated = true;
    field.trainer_party_slot = party_slot;
    Ok(())
}
fn mirror(session: &Session, game: &mut firered_native_prototype::Game) {
    if session.state.events.flag_get(0x230) {
        game.flags.insert("FLAG_GOT_POTION_ON_ROUTE_1".into());
    } else {
        game.flags.remove("FLAG_GOT_POTION_ON_ROUTE_1");
    }
    let id = session.data.ids["ITEM_POTION"];
    let quantity = session.state.bag.pockets[0]
        .iter()
        .find(|s| s.item == id)
        .map_or(0, |s| s.quantity);
    if quantity > 0 {
        game.inventory.insert("ITEM_POTION".into(), quantity);
    } else {
        game.inventory.remove("ITEM_POTION");
    }
}
pub(super) fn update(
    mut field: ResMut<Field>,
    mut simulation: ResMut<Simulation>,
    mut session: ResMut<Session>,
    prefs: Res<Preferences>,
    keys: Res<ButtonInput<KeyCode>>,
    time: Res<Time>,
    replay: Res<Replay>,
    mut battle: ResMut<super::battle_screen::Screen>,
) {
    simulation.game.field_scripts = prefs.field_scripts;
    if prefs.menu {
        return;
    }
    // battle_setup.c resets immunity when entering any battle.
    if battle.active && !replay.active {
        field.encounters.reset_modifiers();
        return;
    }
    if let Err(e) = field.scene(
        &mut simulation,
        &mut session,
        prefs.actor_scenes && prefs.field_scripts,
    ) {
        session.error = format!("Original map scene unavailable: {e}");
        return;
    }
    let started = if let Some((script, name)) = simulation.game.pending_script.take() {
        field.before = Some(session.state.clone());
        field.before_game = Some(simulation.game.clone());
        field.before_actors = Some(field.actors.clone());
        field.selected = field
            .actors
            .actors
            .iter()
            .find(|a| a.object.script == script && a.present)
            .map_or(0, |a| a.object.local_id);
        let face = match simulation.game.facing {
            firered_native_prototype::Cell(0, 1) => 1,
            firered_native_prototype::Cell(0, -1) => 2,
            firered_native_prototype::Cell(-1, 0) => 3,
            _ => 4,
        };
        let _ = session.state.events.var_set(0x800c, face);
        let _ = session.state.events.var_set(0x800f, field.selected);
        field.script_label = script.clone();
        field.name = name;
        match Vm::start(&field.data, &script) {
            Ok(mut vm) => {
                // InitScriptContext clears locals/stack, but preserves comparisonResult.
                vm.comparison = field.comparison;
                field.vm = Some(vm);
                field.waiting = None;
                field.message_ack = false;
            }
            Err(e) => {
                eprintln!("Original script unavailable: {e}");
                simulation.game.script_locked = false;
                simulation.game.dialogue = Some(("Original field script".into(), vec!["This event is not ported yet. Turn off original field scripts in Settings to use the exploration fixture.".into()], 0));
                field.stop();
                return;
            }
        }
        true
    } else {
        false
    };
    let delta = if replay.active && replay.scene {
        1. / firered_native_prototype::TICK_HZ
    } else {
        time.delta_secs_f64().min(0.25)
    };
    field.actor_time += delta;
    let tick_result = (|| -> Result<(), String> {
        while field.actor_time >= 1. / firered_native_prototype::TICK_HZ {
            field.actor_time -= 1. / firered_native_prototype::TICK_HZ;
            let Field { data, actors, .. } = &mut *field;
            actors.tick(data, &mut simulation.game)?;
        }
        Ok(())
    })();
    if let Some(Host::WildBattle) = field.waiting.clone() {
        if battle.active {
            return;
        }
        if let Some(winner) = battle.battle.winner {
            let Session {
                data: core_data,
                state,
                ..
            } = &mut *session;
            // random.c exposes one global RNG. The bounded battle engine records how
            // many calls it made so the persistent field stream can resume exactly
            // where the resolved battle left it.
            state.rng.state = battle.battle.rng.state;
            state.rng.calls = state
                .rng
                .calls
                .wrapping_add(u64::from(battle.battle.rng.calls));
            if winner == 0 {
                if let Some(pmon) = state.party.get_mut(field.wild_party_slot) {
                    battle.battle.mons[0].sync_to_pokemon(pmon);
                }
                if battle.battle.caught {
                    if let Some(mut captured) = field.wild_mon.take() {
                        battle.battle.mons[1].sync_to_pokemon(&mut captured);
                        let caught_species = captured.species;
                        let caught_personality = captured.personality;
                        match state.give_mon(core_data, captured) {
                            Ok(firered_native_prototype::core::GiveMonResult::Party) => {
                                if let Err(e) =
                                    state.pokedex.set_caught(caught_species, caught_personality)
                                {
                                    session.error = format!("Pokédex update unavailable: {e}");
                                }
                            }
                            Ok(firered_native_prototype::core::GiveMonResult::Pc {
                                box_id,
                                ..
                            }) => {
                                if let Err(e) =
                                    state.pokedex.set_caught(caught_species, caught_personality)
                                {
                                    session.error = format!("Pokédex update unavailable: {e}");
                                }
                                simulation.game.dialogue = Some((
                                    "Pokémon Storage".into(),
                                    vec![format!(
                                        "The caught Pokémon was transferred to Box {}.",
                                        box_id + 1
                                    )],
                                    0,
                                ));
                            }
                            Ok(firered_native_prototype::core::GiveMonResult::Full) => {
                                session.error =
                                    "Capture unavailable: Pokémon storage is full".into();
                            }
                            Err(e) => session.error = format!("Capture unavailable: {e}"),
                        }
                    }
                }
            } else if winner == 1 {
                match whiteout_respawn(state.respawn) {
                    Ok((map, cell)) => match state.whiteout(core_data) {
                        Ok(loss) => {
                            simulation.game.map = map.into();
                            simulation.game.position = cell;
                            simulation.game.elevation = simulation.world.map(map).elevation(cell);
                            let recovery = if state.respawn == 1 {
                                "RED scurried back home, protecting the exhausted and fainted Pokémon from further harm…"
                            } else {
                                "RED scurried to a Pokémon Center, protecting the exhausted and fainted Pokémon from further harm…"
                            };
                            simulation.game.dialogue = Some((
                                "Whiteout".into(),
                                vec![
                                    format!("RED paid ¥{loss} as prize money… RED whited out!"),
                                    recovery.into(),
                                ],
                                0,
                            ));
                        }
                        Err(e) => session.error = format!("Whiteout unavailable: {e}"),
                    },
                    Err(e) => session.error = format!("Whiteout unavailable: {e}"),
                }
            } else if winner == 2 {
                if let Some(pmon) = state.party.get_mut(field.wild_party_slot) {
                    battle.battle.mons[0].sync_to_pokemon(pmon);
                }
            }
        }
        field.waiting = None;
        field.wild_mon = None;
        simulation.game.script_locked = false;
    }
    let cur_cell = (simulation.game.map.clone(), simulation.game.position);
    if simulation.game.motion.is_none() && field.last_step_cell.as_ref() != Some(&cur_cell) {
        let previous = field.last_step_cell.replace(cur_cell.clone());
        // Initial placement, map loads and scripted warps are not walking steps.
        // Map entry resets immunity but preserves the private RNG and previous behavior.
        let map_changed = previous.as_ref().is_none_or(|(map, _)| map != &cur_cell.0);
        if map_changed {
            field.encounters.reset_modifiers();
        }
        let walked = previous.is_some_and(|(map, cell)| {
            map == cur_cell.0
                && (cell.0 - cur_cell.1 .0).abs() <= 1
                && (cell.1 - cur_cell.1 .1).abs() <= 1
        });
        if walked
            && prefs.field_scripts
            && !field.active()
            && !started
            && simulation.game.dialogue.is_none()
            && !simulation.game.script_locked
            && field.waiting.is_none()
            && !replay.active
        {
            let map = simulation.world.map(&simulation.game.map);
            let behavior = map.behavior(simulation.game.position);
            let secret_base = map.source.contains("SecretBase_");
            if !firered_native_prototype::encounter::forced_movement(behavior) {
                let poison = match session.state.field_poison_step(secret_base) {
                    Ok(result) => result,
                    Err(e) => {
                        session.error = format!("Original field poison unavailable: {e}");
                        return;
                    }
                };
                if !poison.fainted.is_empty() {
                    let mut pages = vec![
                        "A party Pokémon fainted from poison.".to_string();
                        poison.fainted.len()
                    ];
                    if session.state.party.iter().all(|mon| mon.hp == 0) {
                        let respawn = session.state.respawn;
                        let (map, cell) = match whiteout_respawn(respawn) {
                            Ok(value) => value,
                            Err(e) => {
                                session.error = format!("Poison whiteout unavailable: {e}");
                                return;
                            }
                        };
                        let loss = {
                            let Session { data, state, .. } = &mut *session;
                            match state.whiteout(data) {
                                Ok(loss) => loss,
                                Err(e) => {
                                    session.error = format!("Poison whiteout unavailable: {e}");
                                    return;
                                }
                            }
                        };
                        simulation.game.map = map.into();
                        simulation.game.position = cell;
                        simulation.game.elevation = simulation.world.map(map).elevation(cell);
                        pages.push(format!("RED paid ¥{loss} as prize money… RED whited out!"));
                        pages.push(if respawn == 1 {
                            "RED scurried back home, protecting the exhausted and fainted Pokémon from further harm…".into()
                        } else {
                            "RED scurried to a Pokémon Center, protecting the exhausted and fainted Pokémon from further harm…".into()
                        });
                    }
                    simulation.game.dialogue = Some(("Poison".into(), pages, 0));
                    // EventScript_FieldPoison preempts Repel and encounter checks.
                    return;
                }
            }
        }
        if walked
            && prefs.field_scripts
            && prefs.battle_slice
            && !field.active()
            && !started
            && simulation.game.dialogue.is_none()
            && !simulation.game.script_locked
            && field.waiting.is_none()
            && !replay.active
        {
            let map = simulation.world.map(&simulation.game.map);
            let behavior = map.behavior(simulation.game.position);
            let terrain = map.encounter_type(simulation.game.position);
            let tower = map.source.contains("PokemonTower_");
            let step_result = (|| -> Result<(), String> {
                // field_control_avatar.c: Repel expires before the encounter test.
                // Union Room/playback and forced-avatar movement are not native modes yet.
                if !firered_native_prototype::encounter::forced_movement(behavior)
                    && session.state.events.update_repel_counter(false, false)?
                {
                    simulation.game.pending_script =
                        Some(("EventScript_RepelWoreOff".into(), "Repel".into()));
                    simulation.game.script_locked = true;
                    return Ok(());
                }
                let Some(party_slot) = session.state.party.iter().position(|p| p.hp > 0) else {
                    return Ok(());
                };
                let modifiers = firered_native_prototype::encounter::RateModifiers::from_party(
                    &session.data,
                    &session.state,
                );
                let step = firered_native_prototype::encounter::LandStep {
                    behavior,
                    eligible_terrain: terrain == 1,
                    repel_steps: session.state.events.var_get(0x4020)?,
                    repel_lead_level: Some(session.state.party[party_slot].level),
                    modifiers,
                    ..Default::default()
                };
                let Field {
                    encounter_data,
                    encounters,
                    ..
                } = &mut *field;
                let selected = encounter_data.land_step(&cur_cell.0, encounters, step, || {
                    u32::from(session.state.rng.next16())
                });
                let Some((species_name, level)) = selected else {
                    return Ok(());
                };
                // Tower ghosts and Safari battles need their own battle initialization.
                if tower || session.state.events.flag_get(0x800) {
                    return Err("Tower ghost/Safari battle initialization is not ported".into());
                }
                if !battle
                    .data
                    .species
                    .iter()
                    .any(|s| s.name.eq_ignore_ascii_case(&species_name))
                {
                    return Err(format!("Battle species {species_name} is not ported"));
                }
                let player = firered_native_prototype::battle::Mon::from_pokemon(
                    &battle.data,
                    &session.data,
                    &session.state.party[party_slot],
                )?;
                let Session { data, state, .. } = &mut *session;
                let wild = firered_native_prototype::encounter::create_land_mon(
                    data,
                    state,
                    &species_name,
                    level,
                )?;
                let opponent =
                    firered_native_prototype::battle::Mon::from_pokemon(&battle.data, data, &wild)?;
                let catch_rate = data
                    .species
                    .get(&wild.species)
                    .and_then(|species| species.fields.get("catchRate"))
                    .ok_or("Missing source catch rate")?
                    .parse::<u16>()
                    .map_err(|_| "Invalid source catch rate")?;
                let catch_context = firered_native_prototype::battle::CatchContext {
                    target_is_water_or_bug: data.species_has_type(wild.species, 11)?
                        || data.species_has_type(wild.species, 6)?,
                    // FireRed has no underwater map type, so Dive Ball remains x1.
                    underwater: false,
                    target_previously_caught: state.pokedex.caught(wild.species),
                };
                state.pokedex.set_seen(wild.species, wild.personality)?;
                battle.battle = firered_native_prototype::battle::Battle::wild(
                    &battle.data,
                    player,
                    opponent,
                    catch_rate,
                    state.rng.state,
                );
                battle.battle.set_catch_context(catch_context);
                battle.active = true;
                battle.integrated = true;
                // Persistence must wait for the world-party battle result.
                simulation.game.script_locked = true;
                field.wild_mon = Some(wild);
                field.wild_party_slot = party_slot;
                field.waiting = Some(Host::WildBattle);
                Ok(())
            })();
            if let Err(e) = step_result {
                session.error = format!("Original wild encounter unavailable: {e}");
                eprintln!("{}", session.error);
            }
            if battle.active || simulation.game.pending_script.is_some() {
                return;
            }
        }
    }
    if !field.active() {
        if let Err(e) = tick_result {
            session.error = format!("Actor update failed: {e}");
        }
        return;
    }
    let advance =
        !started && (keys.just_pressed(KeyCode::Space) || keys.just_pressed(KeyCode::Enter));
    let result = (|| -> Result<(), String> {
        tick_result?;
        if let Some(wait) = field.waiting.clone() {
            match wait {
                Host::WaitMovement { id, map } => {
                    field.target(map)?;
                    if !field.actors.finished(id) {
                        return Ok(());
                    }
                }
                Host::WaitMessage | Host::WaitButton => {
                    if !advance {
                        return Ok(());
                    }
                    if let Some((_, pages, index)) = &mut simulation.game.dialogue {
                        if *index + 1 < pages.len() {
                            *index += 1;
                            return Ok(());
                        }
                    }
                    simulation.game.dialogue = None;
                    if wait == Host::WaitMessage {
                        field.message_ack = true;
                    }
                }
                Host::Delay(_) => {
                    field.remaining -= delta;
                    if field.remaining > 0. {
                        return Ok(());
                    }
                }
                Host::YesNo(_, _) => {
                    let choice = if keys.just_pressed(KeyCode::Digit1) {
                        Some(1)
                    } else if keys.just_pressed(KeyCode::Digit2) {
                        Some(0)
                    } else {
                        None
                    };
                    let Some(choice) = choice else {
                        return Ok(());
                    };
                    session.state.events.var_set(0x800d, choice)?;
                    simulation.game.dialogue = None;
                }
                Host::Multichoice { .. } => {
                    let choice = if keys.just_pressed(KeyCode::Digit1) {
                        Some(0)
                    } else if keys.just_pressed(KeyCode::Digit2) {
                        Some(1)
                    } else if keys.just_pressed(KeyCode::Escape) {
                        Some(0x7f)
                    } else {
                        None
                    };
                    let Some(choice) = choice else {
                        return Ok(());
                    };
                    session.state.events.var_set(0x800d, choice)?;
                    simulation.game.dialogue = None;
                }
                Host::TrainerBattle {
                    mode,
                    trainer,
                    flags,
                } => {
                    if battle.active {
                        if replay.active {
                            let screen = &mut *battle;
                            while screen.battle.winner.is_none() {
                                let _ = screen.battle.play(&screen.data, 0);
                            }
                            screen.active = false;
                        } else {
                            return Ok(());
                        }
                    }
                    if let Some(winner) = battle.battle.winner {
                        session.state.rng.state = battle.battle.rng.state;
                        session.state.rng.calls = session
                            .state
                            .rng
                            .calls
                            .wrapping_add(u64::from(battle.battle.rng.calls));
                        let pmon = session
                            .state
                            .party
                            .get_mut(field.trainer_party_slot)
                            .ok_or("Trainer battle party slot disappeared")?;
                        battle.battle.mons[0].sync_to_pokemon(pmon);
                        if winner == 0 {
                            let fixture = trainer_fixture(trainer)?;
                            let prize = if battle.battle.mons[0].double_prize {
                                fixture.prize * 2
                            } else {
                                fixture.prize
                            };
                            session.state.add_money(prize);
                            // ReturnFromBattle stores B_OUTCOME_WON; the early-rival
                            // callback replaces it with FALSE for its script branch.
                            session
                                .state
                                .events
                                .var_set(0x800d, if mode == 9 { 0 } else { 1 })?;
                            session.state.events.flag_set(0x500 + trainer);
                        } else if winner == 1 {
                            session
                                .state
                                .events
                                .var_set(0x800d, if mode == 9 { 1 } else { 2 })?;
                            // battle_setup.c:CB2_EndTrainerBattle only lets an early
                            // rival loss continue when RIVAL_BATTLE_HEAL_AFTER is set.
                            if mode == 9 && flags & 1 != 0 {
                                let sess = &mut *session;
                                sess.state.heal_party(&sess.data)?;
                                sess.state.events.flag_set(0x500 + trainer);
                            } else {
                                let respawn = session.state.respawn;
                                let (map, cell) = whiteout_respawn(respawn)?;
                                let loss = {
                                    let Session { data, state, .. } = &mut *session;
                                    state.whiteout(data)?
                                };
                                simulation.game.map = map.into();
                                simulation.game.position = cell;
                                simulation.game.elevation =
                                    simulation.world.map(map).elevation(cell);
                                let recovery = if respawn == 1 {
                                    "RED scurried back home, protecting the exhausted and fainted Pokémon from further harm…"
                                } else {
                                    "RED scurried to a Pokémon Center, protecting the exhausted and fainted Pokémon from further harm…"
                                };
                                simulation.game.dialogue = Some((
                                    "Whiteout".into(),
                                    vec![
                                        format!("RED paid ¥{loss} as prize money… RED whited out!"),
                                        recovery.into(),
                                    ],
                                    0,
                                ));
                                field.stop();
                                simulation.game.script_locked = false;
                                mirror(&session, &mut simulation.game);
                                return Ok(());
                            }
                        } else {
                            return Err(format!("Invalid trainer battle outcome {winner}"));
                        }
                    }
                }
                Host::WildBattle => {}
                _ => return Err("Unimplemented field wait adapter".into()),
            }
            field.waiting = None;
        }
        for _ in 0..128 {
            let Field { data, vm, .. } = &mut *field;
            let Session {
                data: core, state, ..
            } = &mut *session;
            match vm
                .as_mut()
                .ok_or("Missing script VM")?
                .run(data, core, state, 256)?
            {
                Step::End => {
                    // Keep the adapter locked until parallel movement reaches a save boundary.
                    if field.actors.busy() {
                        return Ok(());
                    }
                    field.comparison = field
                        .vm
                        .as_ref()
                        .map_or(field.comparison, |vm| vm.comparison);
                    field.stop();
                    simulation.game.script_locked = false;
                    simulation.game.dialogue = None;
                    mirror(&session, &mut simulation.game);
                    if prefs.actor_scenes {
                        session
                            .actors
                            .insert(field.actors.map.clone(), field.actors.templates());
                        field.actors.sync(&mut simulation.world);
                        simulation.free =
                            firered_native_prototype::free::Controller::new(&simulation.game);
                    }
                    return Ok(());
                }
                Step::Budget => return Ok(()),
                Step::Host(host) => match host {
                    Host::ApplyMovement { id, address, map } => {
                        field.target(map)?;
                        if field.actors.map.is_empty() {
                            return Err("Enable original actors and map scenes in Settings".into());
                        }
                        field.actors.apply(id, address, &simulation.game);
                    }
                    Host::WaitMovement { id, map } => {
                        field.target(map)?;
                        field.waiting = Some(Host::WaitMovement { id, map });
                        return Ok(());
                    }
                    Host::PlayerGender => {
                        session.state.events.var_set(0x800d, 0)?;
                    } // Existing RED fixture.
                    Host::Object {
                        command,
                        id,
                        values,
                        map,
                    } => {
                        field.target(map)?;
                        if field.actors.map.is_empty() {
                            return Err("Original actor subsystem disabled".into());
                        }
                        if id == 0xff && command == "turnobject" {
                            simulation.game.facing =
                                firered_native_prototype::actors::direction(values[0] as u8)?;
                        } else if let Some(a) = field.actors.object(id as u8 as u16) {
                            match command.as_str() {
                                "addobject" | "addobjectat" => {
                                    a.present = true;
                                    a.at = a.object.at;
                                    a.facing =
                                        firered_native_prototype::actors::facing(a.object.movement);
                                }
                                "removeobject" | "removeobjectat" => a.present = false,
                                "hideobjectat" => a.visible = false,
                                "showobjectat" => a.visible = true,
                                "setobjectxy" => {
                                    a.at = firered_native_prototype::Cell(
                                        values[0] as i16 as i32,
                                        values[1] as i16 as i32,
                                    )
                                }
                                "setobjectxyperm" => {
                                    a.object.at = firered_native_prototype::Cell(
                                        values[0] as i16 as i32,
                                        values[1] as i16 as i32,
                                    )
                                }
                                "copyobjectxytoperm" => a.object.at = a.at,
                                "turnobject" => {
                                    a.facing = firered_native_prototype::actors::direction(
                                        values[0] as u8,
                                    )?
                                }
                                "setobjectmovementtype" => {
                                    a.object.movement = values[0] as u8;
                                    a.original =
                                        firered_native_prototype::actors::facing(a.object.movement);
                                    a.facing = a.original;
                                    a.at = a.object.at;
                                }
                                _ => return Err("Unsupported actor operation".into()),
                            }
                            simulation.dirty = true;
                        }
                    }
                    Host::FacePlayer => {
                        let facing = simulation.game.facing;
                        let selected = field.selected;
                        if let Some(a) = field.actors.object(selected) {
                            a.facing = firered_native_prototype::Cell(-facing.0, -facing.1);
                        }
                    }
                    Host::Lock(_) | Host::Release(_) | Host::Sound(_, _) | Host::WaitSound(_) => {}
                    Host::Message(address) => {
                        let pages = field.text(address)?;
                        simulation.game.dialogue = Some((field.name.clone(), pages, 0));
                        field.message_ack = false;
                    }
                    Host::CloseMessage => simulation.game.dialogue = None,
                    Host::WaitMessage => {
                        field.waiting = Some(Host::WaitMessage);
                        return Ok(());
                    }
                    Host::WaitButton => {
                        if field.message_ack {
                            field.message_ack = false;
                        } else {
                            field.waiting = Some(Host::WaitButton);
                            return Ok(());
                        }
                    }
                    Host::Delay(frames) => {
                        field.remaining = f64::from(frames) / firered_native_prototype::TICK_HZ;
                        field.waiting = Some(Host::Delay(frames));
                        return Ok(());
                    }
                    Host::YesNo(x, y) => {
                        if let Some((_, pages, index)) = &simulation.game.dialogue {
                            let text = format!("{}\n\n1: Yes    2: No", pages[*index]);
                            simulation.game.dialogue = Some((field.name.clone(), vec![text], 0));
                        } else {
                            simulation.game.dialogue =
                                Some((field.name.clone(), vec!["1: Yes    2: No".into()], 0));
                        }
                        field.waiting = Some(Host::YesNo(x, y));
                        return Ok(());
                    }
                    Host::Multichoice {
                        left,
                        top,
                        multichoice_id,
                        ignore_b,
                    } => {
                        if let Some((_, pages, index)) = &simulation.game.dialogue {
                            let text = format!("{}\n\n1: Yes    2: No", pages[*index]);
                            simulation.game.dialogue = Some((field.name.clone(), vec![text], 0));
                        } else {
                            simulation.game.dialogue =
                                Some((field.name.clone(), vec!["1: Yes    2: No".into()], 0));
                        }
                        field.waiting = Some(Host::Multichoice {
                            left,
                            top,
                            multichoice_id,
                            ignore_b,
                        });
                        return Ok(());
                    }
                    Host::Door { .. } | Host::ShowMonPic { .. } | Host::HideMonPic => {}
                    Host::WaitState => {}
                    Host::Warp {
                        map: number,
                        warp_id: _,
                        x,
                        y,
                    } => {
                        let target_map = field.actor_data.map_id(number)?.to_string();
                        simulation.game.map = target_map.clone();
                        simulation.game.position =
                            firered_native_prototype::Cell(x as i32, y as i32);
                        simulation.game.facing = firered_native_prototype::Cell(0, -1);
                        {
                            let Field {
                                actors, actor_data, ..
                            } = &mut *field;
                            actors.load(
                                actor_data,
                                &target_map,
                                &session.state,
                                session.actors.get(&target_map).map_or(&[], Vec::as_slice),
                            )?;
                        }
                        field.actors.sync(&mut simulation.world);
                        simulation.free =
                            firered_native_prototype::free::Controller::new(&simulation.game);
                        simulation.dirty = true;
                        field.stop();
                        simulation.game.script_locked = false;
                        simulation.game.dialogue = None;
                        return Ok(());
                    }
                    Host::TrainerBattle {
                        mode,
                        trainer,
                        flags,
                    } => {
                        start_trainer_battle(&mut field, &mut battle, &mut session, trainer)?;
                        field.waiting = Some(Host::TrainerBattle {
                            mode,
                            trainer,
                            flags,
                        });
                        return Ok(());
                    }
                    Host::WildBattle => return Ok(()),
                    Host::Special { name, destination } => match name.as_str() {
                        "HealPlayerParty" => {
                            let Session { data, state, .. } = &mut *session;
                            state.heal_party(data)?;
                        }
                        "GetQuestLogState" => {
                            let dest = destination.unwrap_or(0x800d);
                            session.state.events.var_set(dest, 0)?;
                        }
                        "GetPokedexCount" => {
                            let dest = destination.unwrap_or(0x800d);
                            let national = session.state.events.var_get(0x8004)? != 0;
                            let seen = session.state.pokedex.count(false, national);
                            let caught = session.state.pokedex.count(true, national);
                            let enabled = u16::from(session.state.national_pokedex_enabled());
                            session.state.events.var_set(0x8005, seen)?;
                            session.state.events.var_set(0x8006, caught)?;
                            session.state.events.var_set(dest, enabled)?;
                        }
                        "IsNationalPokedexEnabled" => {
                            let dest = destination.unwrap_or(0x800d);
                            let enabled = u16::from(session.state.national_pokedex_enabled());
                            session.state.events.var_set(dest, enabled)?;
                        }
                        "EnableNationalPokedex" => {
                            session.state.enable_national_pokedex()?;
                        }
                        "SetSeenMon" => {
                            let species = session.state.events.var_get(0x8004)?;
                            session.state.pokedex.set_seen(species, 0)?;
                        }
                        "IsPlayerNotInTrainerTowerLobby" => {
                            let dest = destination.unwrap_or(0x800d);
                            session.state.events.var_set(dest, 1)?;
                        }
                        "BufferUnionRoomPlayerName" => {
                            let dest = destination.unwrap_or(0x800d);
                            session.state.events.var_set(dest, 0)?;
                        }
                        "UpdatePickStateFromSpecialVar8005" => {
                            if replay.scene {
                                return Err("Oak's still-unported Fame Checker boundary".into());
                            }
                        }
                        "SetVermilionTrashCans" => {
                            let first = ((session.state.rng.next16() % 15) + 1) as i16;
                            let mut second = first;
                            match first {
                                1 => {
                                    if session.state.rng.next16() % 2 == 0 {
                                        second += 1;
                                    } else {
                                        second += 5;
                                    }
                                }
                                2 | 3 | 4 => match session.state.rng.next16() % 3 {
                                    0 => second += 1,
                                    1 => second += 5,
                                    _ => second -= 1,
                                },
                                5 => {
                                    if session.state.rng.next16() % 2 == 0 {
                                        second += 5;
                                    } else {
                                        second -= 1;
                                    }
                                }
                                6 => match session.state.rng.next16() % 3 {
                                    0 => second -= 5,
                                    1 => second += 1,
                                    _ => second += 5,
                                },
                                7 | 8 | 9 => match session.state.rng.next16() % 4 {
                                    0 => second -= 5,
                                    1 => second += 1,
                                    2 => second += 5,
                                    _ => second -= 1,
                                },
                                10 => match session.state.rng.next16() % 3 {
                                    0 => second -= 5,
                                    1 => second += 5,
                                    _ => second -= 1,
                                },
                                11 => {
                                    if session.state.rng.next16() % 2 == 0 {
                                        second -= 5;
                                    } else {
                                        second += 1;
                                    }
                                }
                                12 | 13 | 14 => match session.state.rng.next16() % 3 {
                                    0 => second -= 5,
                                    1 => second += 1,
                                    _ => second -= 1,
                                },
                                15 => {
                                    if session.state.rng.next16() % 2 == 0 {
                                        second -= 5;
                                    } else {
                                        second -= 1;
                                    }
                                }
                                _ => {}
                            }
                            session.state.events.var_set(0x8004, first as u16)?;
                            session.state.events.var_set(0x8005, second as u16)?;
                        }
                        "NullFieldSpecial"
                        | "PlayTrainerEncounterMusic"
                        | "ShowTrainerIntroSpeech"
                        | "ChangePokemonNickname"
                        | "SetFlavorTextFlagFromSpecialVars"
                        | "QuestLog_CutRecording"
                        | "SetUsedPkmnCenterQuestLogEvent"
                        | "StartOldManTutorialBattle"
                        | "DrawWholeMapView"
                        | "SetUnlockedPokedexFlags" => {}
                        _ => return Err(format!("Unported original special: {name}")),
                    },
                },
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        let label = field.script_label.clone();
        field.blocked.insert(label);
        eprintln!("Original field adapter: {error}");
        // An unsupported subsystem must not leave a half-completed reward/story event.
        if let Some(before) = field.before.take() {
            session.state = before;
        }
        if let Some(before) = field.before_game.take() {
            simulation.game = before;
        }
        if let Some(before) = field.before_actors.take() {
            field.actors = before;
        }
        field.stop();
        mirror(&session, &mut simulation.game);
        simulation.game.script_locked = false;
        simulation.game.dialogue = Some((
            "Original field script".into(),
            vec!["This event is not ported yet. Its state changes were discarded.\nTurn off original field scripts in Settings to use the exploration fixture.".into()],
            0,
        ));
    }
    if !field.actors.map.is_empty() {
        let old: Vec<_> = simulation
            .world
            .map(&simulation.game.map)
            .npcs
            .iter()
            .map(|n| (n.local_id, n.invisible))
            .collect();
        field.actors.sync(&mut simulation.world);
        let new: Vec<_> = simulation
            .world
            .map(&simulation.game.map)
            .npcs
            .iter()
            .map(|n| (n.local_id, n.invisible))
            .collect();
        if old != new {
            simulation.dirty = true;
        }
        if let Some(position) = field.actors.player_display(&simulation.game) {
            simulation.displayed = Vec3::new(position[0], 0., position[1]);
        } else if simulation.game.script_locked {
            simulation.displayed = super::cell_position(simulation.game.position);
        }
        if !field.active() {
            simulation.free = firered_native_prototype::free::Controller::new(&simulation.game);
        }
    }
}

/// Headless host integration exercised by packaged --check; never opens user saves.
pub(super) fn check_walking(world: &firered_native_prototype::World) -> Result<(), String> {
    use firered_native_prototype::{
        core::{OtId, Pokemon},
        Cell, Game, Input,
    };
    let map = world.map("route1");
    let index = map
        .encounter_types
        .iter()
        .enumerate()
        .find(|(i, kind)| **kind == 1 && i % map.width() > 0)
        .map(|(i, _)| i)
        .ok_or("Route 1 has no land encounter cell")?;
    let target = Cell((index % map.width()) as i32, (index / map.width()) as i32);
    let before = Cell(target.0 - 1, target.1);
    let mut game = Game::new(world, "route1");
    game.map = "route1".into();
    game.position = before;
    let mut session = Session::open(Default::default(), false, &mut game, world);
    let mon = Pokemon::create(
        &session.data,
        &mut session.state.rng,
        4,
        5,
        31,
        Some(0),
        OtId::Player,
        session.state.trainer_id,
    )?;
    session.state.party.push(mon);
    session.state.events.var_set(0x4020, 1)?;
    let mut prefs = Preferences::load(Default::default());
    prefs.field_scripts = true;
    prefs.battle_slice = true;
    prefs.actor_scenes = false;
    prefs.music = false;
    let mut app = App::new();
    app.insert_resource(Simulation {
        free: firered_native_prototype::free::Controller::new(&game),
        free_vector: [0.; 2],
        free_active: false,
        world: world.clone(),
        game,
        displayed: Vec3::ZERO,
        accumulator: 0.,
        controls: Input::default(),
        moving: false,
        dirty: false,
    })
    .insert_resource(session)
    .insert_resource(prefs)
    .insert_resource(Field::new(0))
    .insert_resource(Replay::new(false, Default::default(), false))
    .insert_resource(super::battle_screen::Screen::new(false))
    .insert_resource(ButtonInput::<KeyCode>::default())
    .insert_resource(Time::<()>::default())
    .add_systems(Update, update);
    app.update();
    assert_eq!(
        app.world()
            .resource::<Session>()
            .state
            .events
            .var_get(0x4020)?,
        1
    );
    app.world_mut().resource_mut::<Simulation>().game.position = target;
    app.update();
    assert_eq!(
        app.world()
            .resource::<Session>()
            .state
            .events
            .var_get(0x4020)?,
        0
    );
    assert_eq!(
        app.world()
            .resource::<Simulation>()
            .game
            .pending_script
            .as_ref()
            .map(|s| s.0.as_str()),
        Some("EventScript_RepelWoreOff")
    );
    assert!(
        !app.world()
            .resource::<super::battle_screen::Screen>()
            .active
    );
    app.update();
    assert!(app.world().resource::<Simulation>().game.dialogue.is_some());

    // Force a successful rate roll to test the real battle bridge, not the UI replay bypass.
    for supported in [true, false] {
        let mut field = Field::new(0);
        field.last_step_cell = Some(("route1".into(), before));
        field.encounters.previous_behavior = map.behavior(target);
        field.encounters.steps_since_encounter = 8;
        for slot in &mut field
            .encounter_data
            .encounters
            .get_mut("route1")
            .unwrap()
            .land
            .as_mut()
            .unwrap()
            .slots
        {
            slot.species = if supported { "Charmander" } else { "MISSING" }.into();
        }
        app.insert_resource(field);
        {
            let mut simulation = app.world_mut().resource_mut::<Simulation>();
            simulation.game.dialogue = None;
            simulation.game.script_locked = false;
            simulation.game.pending_script = None;
        }
        app.world_mut()
            .resource_mut::<super::battle_screen::Screen>()
            .active = false;
        app.update();
        assert_eq!(
            app.world()
                .resource::<super::battle_screen::Screen>()
                .active,
            supported
        );
        if supported {
            assert!(app.world().resource::<Simulation>().game.script_locked);
            assert!(app.world().resource::<Field>().wild_mon.is_some());
            let context = app
                .world()
                .resource::<super::battle_screen::Screen>()
                .battle
                .catch_context;
            assert!(!context.target_is_water_or_bug);
            assert!(!context.underwater);
            assert!(!context.target_previously_caught);
            let (world_rng_state, world_rng_calls) = {
                let session = app.world().resource::<Session>();
                assert!(session.state.pokedex.seen(4));
                assert!(!session.state.pokedex.caught(4));
                (session.state.rng.state, session.state.rng.calls)
            };
            assert_eq!(
                app.world()
                    .resource::<super::battle_screen::Screen>()
                    .battle
                    .rng
                    .state,
                world_rng_state
            );
            {
                let mut session = app.world_mut().resource_mut::<Session>();
                let template = session.state.party[0].clone();
                while session.state.party.len() < 6 {
                    session.state.party.push(template.clone());
                }
            }
            let (resolved_rng_state, resolved_rng_calls) = {
                let mut battle = app
                    .world_mut()
                    .resource_mut::<super::battle_screen::Screen>();
                if !battle.battle.catch(1)? {
                    return Err("Master Ball capture check failed".into());
                }
                battle.active = false;
                (battle.battle.rng.state, battle.battle.rng.calls)
            };
            app.update();
            let session = app.world().resource::<Session>();
            assert_eq!(session.state.rng.state, resolved_rng_state);
            assert_eq!(
                session.state.rng.calls,
                world_rng_calls + u64::from(resolved_rng_calls)
            );
            assert!(session.state.pokedex.caught(4));
            assert!(session.state.storage.boxes[0][0].is_some());
            assert_eq!(
                session.state.storage.boxes[0][0]
                    .as_ref()
                    .map(|mon| mon.poke_ball),
                Some(1)
            );
            assert!(!app.world().resource::<Simulation>().game.script_locked);
        } else {
            assert!(app.world().resource::<Session>().error.contains("MISSING"));
            assert!(app.world().resource::<Field>().wild_mon.is_none());
        }
    }

    // Resolve another walking battle as a loss to cover the source whiteout bridge.
    let mut field = Field::new(0);
    field.last_step_cell = Some(("route1".into(), before));
    field.encounters.previous_behavior = map.behavior(target);
    field.encounters.steps_since_encounter = 8;
    for slot in &mut field
        .encounter_data
        .encounters
        .get_mut("route1")
        .unwrap()
        .land
        .as_mut()
        .unwrap()
        .slots
    {
        slot.species = "Charmander".into();
    }
    app.insert_resource(field);
    {
        let mut simulation = app.world_mut().resource_mut::<Simulation>();
        simulation.game.dialogue = None;
        simulation.game.script_locked = false;
        simulation.game.pending_script = None;
        simulation.game.map = "route1".into();
        simulation.game.position = target;
    }
    let expected_loss = {
        let mut session = app.world_mut().resource_mut::<Session>();
        session.error.clear();
        session.state.respawn = 2;
        session.state.money = 3000;
        session.state.whiteout_money_loss()
    };
    app.world_mut()
        .resource_mut::<super::battle_screen::Screen>()
        .active = false;
    app.update();
    assert!(
        app.world()
            .resource::<super::battle_screen::Screen>()
            .active
    );
    assert!(
        app.world()
            .resource::<super::battle_screen::Screen>()
            .battle
            .catch_context
            .target_previously_caught
    );
    {
        let mut battle = app
            .world_mut()
            .resource_mut::<super::battle_screen::Screen>();
        battle.battle.winner = Some(1);
        battle.active = false;
    }
    app.update();
    let session = app.world().resource::<Session>();
    assert_eq!(session.state.money, 3000 - expected_loss);
    assert!(session.state.party.iter().all(|mon| mon.hp == mon.stats[0]));
    let simulation = app.world().resource::<Simulation>();
    assert_eq!(simulation.game.map, "viridian_center");
    assert_eq!(simulation.game.position, Cell(7, 4));
    assert!(!simulation.game.script_locked);

    // Sammy's one-Pokémon source party is safe for the current trainer slice.
    // Starting the battle registers Weedle as seen, and winning pays the exact
    // Bug Catcher reward with the participating Amulet Coin multiplier.
    assert!(trainer_fixture(102).is_err()); // Rick has two Pokémon.
    assert!(trainer_fixture(329).is_err()); // Route 22 rival has two Pokémon.
    let weedle = {
        let mut session = app.world_mut().resource_mut::<Session>();
        let amulet_coin = session.data.id("ITEM_AMULET_COIN")?;
        let weedle = session.data.id("SPECIES_WEEDLE")?;
        session.state.money = 1000;
        session.state.party[0].held_item = amulet_coin;
        weedle
    };
    let mut trainer_field = Field::new(0);
    trainer_field.vm = Some(Vm::start(&trainer_field.data, "EventScript_Return")?);
    trainer_field.waiting = Some(Host::TrainerBattle {
        mode: 0,
        trainer: 104,
        flags: 0,
    });
    app.insert_resource(trainer_field);
    app.world_mut()
        .resource_mut::<Simulation>()
        .game
        .script_locked = true;
    let started: Result<(), String> =
        app.world_mut()
            .resource_scope(|world, mut field: Mut<Field>| {
                world.resource_scope(|world, mut battle: Mut<super::battle_screen::Screen>| {
                    let mut session = world.resource_mut::<Session>();
                    start_trainer_battle(&mut field, &mut battle, &mut session, 104)
                })
            });
    started?;
    {
        let session = app.world().resource::<Session>();
        assert!(session.state.pokedex.seen(weedle));
    }
    {
        let mut battle = app
            .world_mut()
            .resource_mut::<super::battle_screen::Screen>();
        assert!(battle.battle.mons[0].double_prize);
        battle.battle.winner = Some(0);
        battle.active = false;
    }
    app.update();
    {
        let session = app.world().resource::<Session>();
        assert_eq!(session.state.money, 1216);
        assert_eq!(session.state.events.var_get(0x800d)?, 1);
        assert!(session.state.events.flag_get(0x500 + 104));
    }
    assert!(!app.world().resource::<Simulation>().game.script_locked);

    // The lab rival is an early-rival battle with HEAL_AFTER. A loss resumes
    // its script, heals the selected party mon, sets the trainer flag and
    // returns the battle's exact RNG tail to the field stream.
    let battle_data = firered_native_prototype::battle::Data::source();
    let (player, seed, calls) = {
        let session = app.world().resource::<Session>();
        (
            firered_native_prototype::battle::Mon::from_pokemon(
                &battle_data,
                &session.data,
                &session.state.party[0],
            )?,
            session.state.rng.state,
            session.state.rng.calls,
        )
    };
    let rival = firered_native_prototype::battle::Mon::create_at_level(
        &battle_data,
        1,
        5,
        [0; 6],
        [0; 6],
        battle_data.rival_personality,
    );
    let mut resolved =
        firered_native_prototype::battle::Battle::from_matchup(&battle_data, player, rival, seed);
    resolved.mons[0].hp = 0;
    resolved.rng.next();
    resolved.winner = Some(1);
    let resolved_state = resolved.rng.state;
    let resolved_calls = resolved.rng.calls;
    {
        let mut screen = app
            .world_mut()
            .resource_mut::<super::battle_screen::Screen>();
        screen.battle = resolved;
        screen.active = false;
    }
    let (current_map, current_cell) = {
        let simulation = app.world().resource::<Simulation>();
        (simulation.game.map.clone(), simulation.game.position)
    };
    let mut trainer_field = Field::new(0);
    trainer_field.vm = Some(Vm::start(&trainer_field.data, "EventScript_Return")?);
    trainer_field.waiting = Some(Host::TrainerBattle {
        mode: 9,
        trainer: 326,
        flags: 3,
    });
    trainer_field.trainer_party_slot = 0;
    trainer_field.last_step_cell = Some((current_map.clone(), current_cell));
    app.insert_resource(trainer_field);
    app.world_mut()
        .resource_mut::<Simulation>()
        .game
        .script_locked = true;
    app.update();
    let session = app.world().resource::<Session>();
    assert_eq!(session.state.rng.state, resolved_state);
    assert_eq!(session.state.rng.calls, calls + u64::from(resolved_calls));
    assert_eq!(session.state.events.var_get(0x800d)?, 1);
    assert!(session.state.events.flag_get(0x500 + 326));
    assert!(session.state.party.iter().all(|mon| mon.hp == mon.stats[0]));
    let simulation = app.world().resource::<Simulation>();
    assert_eq!(simulation.game.map, current_map);
    assert_eq!(simulation.game.position, current_cell);
    assert!(!simulation.game.script_locked);

    // field_control_avatar.c runs the five-step poison counter before Repel and
    // encounter checks. A final poisoned party member follows FieldPoison into
    // the same source whiteout state/warp already used by battle losses.
    let poison_from = Cell(7, 4);
    let poison_to = Cell(8, 4);
    let poison_loss = {
        let mut session = app.world_mut().resource_mut::<Session>();
        session.state.party.truncate(1);
        session.state.party[0].hp = 1;
        session.state.party[0].status = 1 << 3;
        session.state.party[0].friendship = 200;
        session.state.money = 3000;
        session.state.respawn = 2;
        session.state.events.var_set(0x4022, 4)?;
        session.state.whiteout_money_loss()
    };
    let mut poison_field = Field::new(0);
    poison_field.last_step_cell = Some(("viridian_center".into(), poison_from));
    app.insert_resource(poison_field);
    app.world_mut().resource_mut::<Preferences>().battle_slice = false;
    {
        let mut simulation = app.world_mut().resource_mut::<Simulation>();
        simulation.game.map = "viridian_center".into();
        simulation.game.position = poison_to;
        simulation.game.dialogue = None;
        simulation.game.script_locked = false;
    }
    app.update();
    {
        let session = app.world().resource::<Session>();
        assert_eq!(session.state.events.var_get(0x4022)?, 0);
        assert_eq!(session.state.money, 3000 - poison_loss);
        assert_eq!(session.state.party[0].friendship, 190);
        assert_eq!(session.state.party[0].status, 0);
        assert_eq!(session.state.party[0].hp, session.state.party[0].stats[0]);
    }
    let simulation = app.world().resource::<Simulation>();
    assert_eq!(simulation.game.map, "viridian_center");
    assert_eq!(simulation.game.position, poison_from);
    assert_eq!(
        simulation
            .game
            .dialogue
            .as_ref()
            .map(|dialogue| dialogue.1.len()),
        Some(3)
    );
    Ok(())
}

#[derive(Resource)]
pub(super) struct Replay {
    active: bool,
    frame: u32,
    phase: u8,
    started: bool,
    path: std::path::PathBuf,
    scene: bool,
}
impl Replay {
    pub fn new(active: bool, path: std::path::PathBuf, scene: bool) -> Self {
        Self {
            active,
            frame: 0,
            phase: 0,
            started: false,
            path,
            scene,
        }
    }
}
pub(super) fn replay(
    mut replay: ResMut<Replay>,
    field: Res<Field>,
    mut simulation: ResMut<Simulation>,
    session: Res<Session>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut exit: MessageWriter<AppExit>,
) {
    if !replay.active {
        return;
    }
    replay.frame += 1;
    keys.reset_all();
    if replay.frame > 600 {
        let _ = std::fs::write(&replay.path, "{\"passed\":false,\"reason\":\"timeout\"}");
        replay.active = false;
        exit.write(AppExit::Error(std::num::NonZeroU8::new(1).unwrap()));
        return;
    }
    if replay.frame < 30 {
        return;
    }
    if replay.scene {
        let mut fail = None;
        match replay.phase {
            0 if !field.active() && simulation.game.dialogue.is_none() => {
                if field.actors.actors.iter().any(|a| {
                    a.object.local_id == 1 && a.at == firered_native_prototype::Cell(5, 15)
                }) {
                    keys.press(KeyCode::Space);
                    replay.phase = 1;
                }
            }
            1 => {
                if field.active() {
                    if replay.frame % 4 == 0 {
                        keys.press(KeyCode::Space);
                    }
                } else if simulation.game.dialogue.is_some() {
                    fail = Some("Original sign-lady event failed");
                } else if session.state.events.flag_get(2) {
                    let moved = field.actors.actors.iter().any(|a| {
                        a.object.local_id == 1 && a.at == firered_native_prototype::Cell(6, 15)
                    });
                    let saved = session.actors.get("pallet").is_some_and(|v| {
                        v.iter().any(|a| {
                            a.local_id == 1 && a.at == firered_native_prototype::Cell(6, 15)
                        })
                    });
                    if !moved || !saved {
                        fail = Some("Actor movement/template save failed");
                    } else {
                        simulation.game.position = firered_native_prototype::Cell(5, 15);
                        simulation.game.facing = firered_native_prototype::Cell(1, 0);
                        simulation.free =
                            firered_native_prototype::free::Controller::new(&simulation.game);
                        keys.press(KeyCode::Space);
                        replay.phase = 2;
                    }
                }
            }
            2 => {
                if field.active() {
                    if replay.frame % 4 == 0 {
                        keys.press(KeyCode::Space);
                    }
                } else if simulation.game.dialogue.is_some() {
                    fail = Some("Repeat sign-lady event failed");
                } else {
                    // Exercise automatic original coordinate dispatch and explicit rollback
                    // at Oak's still-unported Fame Checker boundary.
                    simulation.game.position = firered_native_prototype::Cell(13, 1);
                    simulation.game.facing = firered_native_prototype::Cell(0, -1);
                    simulation.free =
                        firered_native_prototype::free::Controller::new(&simulation.game);
                    replay.phase = 3;
                }
            }
            3 if !field.active() && simulation.game.dialogue.is_some() => {
                if simulation.game.script_locked
                    || simulation.game.position != firered_native_prototype::Cell(13, 1)
                    || session.state.events.var_get(0x4050).unwrap_or(99) != 0
                    || !session.state.events.flag_get(2)
                {
                    fail = Some("Coordinate failure rollback failed");
                } else {
                    let _=std::fs::write(&replay.path,serde_json::json!({"passed":true,"sign_lady_first_and_repeat":true,"actor_step_and_template":true,"map_transition":true,"coordinate_dispatch_and_rollback":true,"frames":replay.frame}).to_string());
                    replay.active = false;
                    exit.write(AppExit::Success);
                }
            }
            _ => {}
        }
        if let Some(reason) = fail {
            let _ = std::fs::write(
                &replay.path,
                serde_json::json!({"passed":false,"reason":reason,"phase":replay.phase})
                    .to_string(),
            );
            replay.active = false;
            exit.write(AppExit::Error(std::num::NonZeroU8::new(1).unwrap()));
        }
        return;
    }
    let game = &simulation.game;
    if replay.phase == 0 {
        keys.press(KeyCode::Space);
        replay.phase = 1;
        return;
    }
    if field.active() {
        replay.started = true;
        if replay.frame % 4 == 0 {
            keys.press(KeyCode::Space);
        }
        return;
    }
    if !replay.started {
        if replay.frame % 4 == 0 {
            keys.press(KeyCode::Space);
        }
        return;
    }
    let id = session.data.ids["ITEM_POTION"];
    let quantity = session.state.bag.pockets[0]
        .iter()
        .find(|s| s.item == id)
        .map_or(0, |s| s.quantity);
    let passed = game.dialogue.is_none()
        && quantity == 1
        && session.state.events.flag_get(0x230)
        && game.inventory.get("ITEM_POTION") == Some(&1)
        && !game.script_locked;
    if !passed || replay.frame > 600 {
        let _ = std::fs::write(&replay.path, "{\"passed\":false}");
        replay.active = false;
        exit.write(AppExit::Error(std::num::NonZeroU8::new(1).unwrap()));
        return;
    }
    if replay.phase == 1 {
        keys.press(KeyCode::Space);
        replay.phase = 2;
        replay.started = false;
        return;
    }
    let report = serde_json::json!({"passed":true,"frames":replay.frame,"original_script":"Route1_EventScript_MartClerk",
        "branches":2,"potions":quantity,"behaviour":"unverified against original C"});
    std::fs::write(&replay.path, serde_json::to_vec_pretty(&report).unwrap())
        .expect("Field integration report");
    replay.active = false;
    exit.write(AppExit::Success);
}
