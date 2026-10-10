//! Optional persistence adapter for the existing exploration fixture.
//! This does not turn the fixture into the original new-game/story sequence.
use super::{Preferences, Simulation};
use bevy::prelude::*;
use firered_native_prototype::{
    Cell, Game, World,
    core::{Data, State},
};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Clone, Serialize, Deserialize, PartialEq)]
struct Position {
    map: String,
    cell: Cell,
    facing: Cell,
    running_shoes: bool,
}
impl Position {
    fn capture(game: &Game) -> Self {
        Self {
            map: game.map.clone(),
            cell: game.position,
            facing: game.facing,
            running_shoes: game.running_shoes,
        }
    }
}
#[derive(Serialize, Deserialize)]
struct Save {
    state: State,
    exploration_fixture: Position,
    #[serde(default)]
    actors: std::collections::HashMap<String, Vec<firered_native_prototype::actors::Template>>,
    #[serde(default)]
    actor_initialized: bool,
}
#[derive(Resource)]
pub(super) struct Session {
    pub(super) data: Data,
    pub(super) state: State,
    pub(super) actors:
        std::collections::HashMap<String, Vec<firered_native_prototype::actors::Template>>,
    pub(super) actor_initialized: bool,
    path: PathBuf,
    last: String,
    active: bool,
    pub error: String,
}
impl Session {
    /// Reset progress at the current location, retaining the previous save for recovery.
    pub fn reset(&mut self, game: &mut Game, enabled: bool) -> Result<(), String> {
        let backup = self.path.with_extension(format!(
            "reset-{}.bak",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(|e| e.to_string())?
                .as_nanos()
        ));
        let had_save = self.path.exists();
        if had_save {
            std::fs::rename(&self.path, &backup).map_err(|e| e.to_string())?;
        }
        let old_state = self.state.clone();
        let old_actors = std::mem::take(&mut self.actors);
        let old_initialized = self.actor_initialized;
        self.actor_initialized = false;
        let old_last = self.last.clone();
        let mut fresh = game.clone();
        fresh.flags.clear();
        fresh.inventory.clear();
        fresh.dialogue = None;
        fresh.pending_script = None;
        fresh.script_locked = false;
        self.state = State::new(&self.data, 42, 1234);
        self.last.clear();
        if let Err(e) = self.store(&fresh) {
            self.state = old_state;
            self.actors = old_actors;
            self.actor_initialized = old_initialized;
            self.last = old_last;
            if had_save {
                std::fs::rename(&backup, &self.path)
                    .map_err(|restore| format!("{e}; restore failed: {restore}"))?;
            }
            return Err(e);
        }
        *game = fresh;
        self.active = enabled;
        self.error.clear();
        Ok(())
    }
    pub fn open(path: PathBuf, enabled: bool, game: &mut Game, world: &World) -> Self {
        let data = Data::source();
        let state = State::new(&data, 42, 1234);
        let mut session = Self {
            data,
            state,
            actors: Default::default(),
            actor_initialized: false,
            path,
            last: String::new(),
            active: enabled,
            error: String::new(),
        };
        if enabled && session.path.exists() {
            let result = (|| -> Result<Save, String> {
                let bytes = std::fs::read(&session.path).map_err(|e| e.to_string())?;
                let saved: Save = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
                saved.state.validate(&session.data)?;
                let actor_data = firered_native_prototype::actors::Data::source();
                for (map_id, templates) in &saved.actors {
                    let definitions = actor_data
                        .maps
                        .get(map_id)
                        .ok_or("Unsupported saved actor map")?;
                    let map = world
                        .maps
                        .iter()
                        .find(|m| &m.id == map_id)
                        .ok_or("Unsupported saved actor map")?;
                    let mut ids = std::collections::HashSet::new();
                    for t in templates {
                        if !ids.insert(t.local_id)
                            || !definitions.objects.iter().any(|o| o.local_id == t.local_id)
                            || t.at.0 < 0
                            || t.at.1 < 0
                            || t.at.0 >= map.width() as i32
                            || t.at.1 >= map.height() as i32
                            || t.movement >= 0x51
                        {
                            return Err("Invalid saved actor template".into());
                        }
                    }
                }
                let pos = &saved.exploration_fixture;
                let map = world
                    .maps
                    .iter()
                    .find(|m| m.id == pos.map)
                    .ok_or("Unsupported saved map")?;
                if !map.terrain_walkable(pos.cell)
                    || ![
                        Cell(0, 1),
                        Cell(0, -1),
                        Cell(1, 0),
                        Cell(-1, 0),
                        Cell(1, 1),
                        Cell(-1, 1),
                        Cell(1, -1),
                        Cell(-1, -1),
                    ]
                    .contains(&pos.facing)
                {
                    return Err("Invalid saved location".into());
                }
                Ok(saved)
            })();
            match result {
                Ok(saved) => {
                    game.map = saved.exploration_fixture.map;
                    game.position = saved.exploration_fixture.cell;
                    game.facing = saved.exploration_fixture.facing;
                    game.running_shoes = saved.exploration_fixture.running_shoes;
                    game.elevation = world.map(&game.map).elevation(game.position);
                    if saved.state.events.flag_get(0x230) {
                        game.flags.insert("FLAG_GOT_POTION_ON_ROUTE_1".into());
                    }
                    let id = session.data.ids["ITEM_POTION"];
                    if let Some(slot) = saved.state.bag.pockets[0].iter().find(|s| s.item == id) {
                        game.inventory.insert("ITEM_POTION".into(), slot.quantity);
                    }
                    session.state = saved.state;
                    session.actors = saved.actors;
                    session.actor_initialized = saved.actor_initialized;
                }
                Err(e) => {
                    session.active = false;
                    session.error =
                        format!("Native save unavailable: {e}. Existing file preserved.");
                }
            }
        }
        session
    }
    fn store(&mut self, game: &Game) -> Result<(), String> {
        let position = Position::capture(game);
        self.state.map = position.map.clone();
        self.state.position = [position.cell.0, position.cell.1];
        if game.flags.contains("FLAG_GOT_POTION_ON_ROUTE_1") {
            self.state.events.flag_set(0x230);
        }
        let id = self.data.ids["ITEM_POTION"];
        let quantity = game.inventory.get("ITEM_POTION").copied().unwrap_or(0);
        let old = self.state.bag.pockets[0]
            .iter()
            .find(|s| s.item == id)
            .map_or(0, |s| s.quantity);
        if quantity > old
            && !self
                .state
                .bag
                .add(&self.data, &mut self.state.events, id, quantity - old)
        {
            return Err("Cannot persist exploration inventory".into());
        }
        self.state.validate(&self.data)?;
        let fingerprint = serde_json::to_string(&(
            position.clone(),
            &game.flags,
            &game.inventory,
            &self.state,
            &self.actors,
            self.actor_initialized,
        ))
        .map_err(|e| e.to_string())?;
        if self.last == fingerprint {
            return Ok(());
        }
        let saved = Save {
            state: self.state.clone(),
            exploration_fixture: position,
            actors: self.actors.clone(),
            actor_initialized: self.actor_initialized,
        };
        let bytes = serde_json::to_vec_pretty(&saved).map_err(|e| e.to_string())?;
        let pending = self.path.with_extension("pending");
        std::fs::write(&pending, bytes).map_err(|e| e.to_string())?;
        std::fs::rename(pending, &self.path).map_err(|e| e.to_string())?;
        self.last = fingerprint;
        Ok(())
    }
}
pub(super) fn update(
    mut session: ResMut<Session>,
    simulation: Res<Simulation>,
    prefs: Res<Preferences>,
) {
    if !session.active
        || !prefs.native_state
        || simulation.game.busy()
        || simulation.game.dialogue.is_some()
    {
        return;
    }
    if let Err(e) = session.store(&simulation.game) {
        session.error = format!("Native save failed: {e}");
        session.active = false;
    }
}

/// Startup integration only: save and restore the existing field adapter.
pub(super) fn check_world(world: &World, directory: &std::path::Path) -> Result<(), String> {
    let path = directory.join(format!("native-integration-{}.json", std::process::id()));
    let result = (|| -> Result<(), String> {
        let mut game = Game::new(world, "pallet");
        let mut session = Session::open(path.clone(), true, &mut game, world);
        game.flags.insert("FLAG_GOT_POTION_ON_ROUTE_1".into());
        game.inventory.insert("ITEM_POTION".into(), 1);
        session.actors.insert(
            "pallet".into(),
            vec![firered_native_prototype::actors::Template {
                local_id: 1,
                at: Cell(6, 15),
                movement: 7,
            }],
        );
        session.actor_initialized = true;
        session.store(&game)?;
        game.facing = Cell(1, 0);
        session.store(&game)?;
        let mut restored = Game::new(world, "pallet");
        let mut loaded = Session::open(path.clone(), true, &mut restored, world);
        if !loaded.error.is_empty()
            || Position::capture(&game) != Position::capture(&restored)
            || game.inventory != restored.inventory
            || game.flags != restored.flags
            || loaded.actors != session.actors
            || !loaded.actor_initialized
        {
            return Err("Exploration persistence integration failed".into());
        }
        loaded.reset(&mut restored, true)?;
        let mut reset_game = Game::new(world, "pallet");
        let reset = Session::open(path.clone(), true, &mut reset_game, world);
        if !reset.error.is_empty()
            || !reset_game.flags.is_empty()
            || !reset_game.inventory.is_empty()
            || reset.state.events.flag_get(0x230)
            || !reset.actors.is_empty()
            || reset.actor_initialized
            || Position::capture(&restored) != Position::capture(&game)
        {
            return Err("Native save reset integration failed".into());
        }
        let bytes = b"invalid native save";
        std::fs::write(&path, bytes).map_err(|e| e.to_string())?;
        let rejected = Session::open(path.clone(), true, &mut restored, world);
        if rejected.active
            || rejected.error.is_empty()
            || std::fs::read(&path).map_err(|e| e.to_string())? != bytes
        {
            return Err("Invalid native save was not preserved".into());
        }
        Ok(())
    })();
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("pending"));
    if let Ok(entries) = std::fs::read_dir(directory) {
        let prefix = format!("native-integration-{}.reset-", std::process::id());
        for entry in entries.flatten() {
            if entry.file_name().to_string_lossy().starts_with(&prefix) {
                let _ = std::fs::remove_file(entry.path());
            }
        }
    }
    result
}
