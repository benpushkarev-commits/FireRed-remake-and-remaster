//! Partial native script movement and map event tables from the pinned source.
//! Ambient wandering, held-movement frame parity and effect sprites are deferred.
use crate::{Cell, Game, Npc, World, core::State, field::Data as Scripts};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Deserialize)]
pub struct Object {
    pub local_id: u16,
    pub at: Cell,
    pub elevation: u8,
    pub flag: u16,
    pub movement: u8,
    pub graphics: String,
    pub script: String,
    pub trainer_type: u8,
    pub trainer_sight: u8,
}
#[derive(Deserialize)]
pub struct Coord {
    pub at: Cell,
    pub elevation: u8,
    pub variable: u16,
    pub value: u16,
    pub script: String,
}
#[derive(Deserialize)]
pub struct Map {
    pub number: [u8; 2],
    pub script: String,
    pub objects: Vec<Object>,
    pub coords: Vec<Coord>,
}
#[derive(Deserialize)]
pub struct Data {
    pub revision: String,
    pub maps: HashMap<String, Map>,
}
impl Data {
    pub fn source() -> Self {
        serde_json::from_str(include_str!("../data/actors.json")).expect("Original actor data")
    }
    pub fn map_id(&self, number: [u8; 2]) -> Result<&str, String> {
        self.maps
            .iter()
            .find(|(_, m)| m.number == number)
            .map(|(id, _)| id.as_str())
            .ok_or("Script targets a map outside the native slice".into())
    }
    pub fn table(
        &self,
        scripts: &Scripts,
        map: &str,
        tag: u8,
        state: &State,
    ) -> Result<Option<u32>, String> {
        let mut p = scripts.label(&self.maps.get(map).ok_or("Unknown actor map")?.script)?;
        loop {
            let kind = byte(scripts, p)?;
            if kind == 0 {
                return Ok(None);
            }
            if kind == tag {
                let mut address = word(scripts, p + 1)?;
                if tag != 2 && tag != 4 {
                    return Ok(Some(address));
                }
                loop {
                    let a = half(scripts, address)?;
                    if a == 0 {
                        return Ok(None);
                    }
                    let b = half(scripts, address + 2)?;
                    if state.events.var_get(a)? == state.events.var_get(b)? {
                        return Ok(Some(word(scripts, address + 4)?));
                    }
                    address += 8;
                }
            }
            p += 5;
        }
    }
}
fn byte(data: &Scripts, p: u32) -> Result<u8, String> {
    data.bytes
        .get(p as usize)
        .copied()
        .ok_or("Actor script outside archive".into())
}
fn half(data: &Scripts, p: u32) -> Result<u16, String> {
    Ok(u16::from_le_bytes([byte(data, p)?, byte(data, p + 1)?]))
}
fn word(data: &Scripts, p: u32) -> Result<u32, String> {
    Ok(u32::from_le_bytes([
        byte(data, p)?,
        byte(data, p + 1)?,
        byte(data, p + 2)?,
        byte(data, p + 3)?,
    ]))
}
pub fn direction(n: u8) -> Result<Cell, String> {
    match n {
        1 => Ok(Cell(0, 1)),
        2 => Ok(Cell(0, -1)),
        3 => Ok(Cell(-1, 0)),
        4 => Ok(Cell(1, 0)),
        _ => Err("Unsupported actor direction".into()),
    }
}
pub fn facing(movement: u8) -> Cell {
    match movement {
        7 => Cell(0, -1),
        9 => Cell(-1, 0),
        10 => Cell(1, 0),
        _ => Cell(0, 1),
    }
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Template {
    pub local_id: u16,
    pub at: Cell,
    pub movement: u8,
}
#[derive(Clone)]
pub struct Actor {
    pub object: Object,
    pub at: Cell,
    pub facing: Cell,
    pub visible: bool,
    pub present: bool,
    pub original: Cell,
    pub emote: u16,
    task: Option<Task>,
}
#[derive(Clone)]
struct Task {
    pc: u32,
    remaining: u16,
    duration: u16,
    from: Cell,
    to: Cell,
}
#[derive(Clone, Default)]
pub struct Actors {
    pub map: String,
    pub actors: Vec<Actor>,
    player: Option<Task>,
    last_moving: u16,
}
impl Actors {
    pub fn load(
        &mut self,
        data: &Data,
        map: &str,
        state: &State,
        saved: &[Template],
    ) -> Result<(), String> {
        self.map = map.into();
        self.player = None;
        self.actors = data
            .maps
            .get(map)
            .ok_or("Unknown actor map")?
            .objects
            .iter()
            .map(|object| {
                let mut object = object.clone();
                if let Some(t) = saved.iter().find(|t| t.local_id == object.local_id) {
                    object.at = t.at;
                    object.movement = t.movement;
                }
                let face = facing(object.movement);
                Actor {
                    at: object.at,
                    facing: face,
                    original: face,
                    present: object.flag == 0 || !state.events.flag_get(object.flag),
                    visible: object.movement != 0x4c,
                    object,
                    task: None,
                    emote: 0,
                }
            })
            .collect();
        Ok(())
    }
    pub fn templates(&self) -> Vec<Template> {
        self.actors
            .iter()
            .map(|a| Template {
                local_id: a.object.local_id,
                at: a.object.at,
                movement: a.object.movement,
            })
            .collect()
    }
    pub fn object(&mut self, id: u16) -> Option<&mut Actor> {
        self.actors.iter_mut().find(|a| a.object.local_id == id)
    }
    pub fn apply(&mut self, id: u16, pc: u32, game: &Game) {
        let id = id as u8 as u16; // Original movement API narrows local IDs to u8.
        self.last_moving = id;
        let at = if id == 0xff {
            Some(game.position)
        } else {
            self.actors
                .iter()
                .find(|a| a.object.local_id == id && a.present)
                .map(|a| a.at)
        };
        if let Some(at) = at {
            let task = Some(Task {
                pc,
                remaining: 0,
                duration: 0,
                from: at,
                to: at,
            });
            if id == 0xff {
                if self.player.is_none() {
                    self.player = task;
                }
            } else if let Some(a) = self.object(id) {
                if a.task.is_none() {
                    a.task = task;
                }
            }
        }
    }
    pub fn finished(&mut self, id: u16) -> bool {
        if id != 0 {
            self.last_moving = id as u8 as u16;
        }
        if self.last_moving == 0xff {
            self.player.is_none()
        } else {
            self.actors
                .iter()
                .find(|a| a.object.local_id == self.last_moving && a.present)
                .is_none_or(|a| a.task.is_none())
        }
    }
    pub fn clear_tasks(&mut self) {
        self.player = None;
        for a in &mut self.actors {
            a.task = None;
        }
    }
    pub fn busy(&self) -> bool {
        self.player.is_some() || self.actors.iter().any(|a| a.task.is_some())
    }
    pub fn sync(&self, world: &mut World) {
        let Some(map) = world.maps.iter_mut().find(|m| m.id == self.map) else {
            return;
        };
        map.npcs = self
            .actors
            .iter()
            .filter(|a| a.present)
            .map(|a| Npc {
                at: a.at,
                name: a
                    .object
                    .graphics
                    .replace("OBJ_EVENT_GFX_", "")
                    .replace('_', " "),
                lines: Vec::new(),
                script: a.object.script.clone(),
                elevation: a.object.elevation,
                movement: format!("{}", a.object.movement),
                graphics: a.object.graphics.clone(),
                local_id: a.object.local_id,
                invisible: !a.visible,
            })
            .collect();
    }
    pub fn displayed(&self, id: u16) -> Option<([f32; 2], Cell)> {
        self.actors
            .iter()
            .find(|a| a.object.local_id == id && a.present && a.visible)
            .map(|a| (display(a.at, a.task.as_ref()), a.facing))
    }
    pub fn player_display(&self, game: &Game) -> Option<[f32; 2]> {
        self.player
            .as_ref()
            .map(|t| display(game.position, Some(t)))
    }
    pub fn tick(&mut self, data: &Scripts, game: &mut Game) -> Result<(), String> {
        if let Some(task) = &mut self.player {
            let facing = game.facing;
            let position = game.position;
            if step(
                task,
                data,
                &mut game.position,
                &mut game.facing,
                facing,
                position,
            )? {
                self.player = None;
            }
        }
        for a in &mut self.actors {
            a.emote = a.emote.saturating_sub(1);
            if let Some(t) = &mut a.task {
                if t.remaining == 0 {
                    let next = byte(data, t.pc)?;
                    if next == 0x62 || next == 0x63 {
                        // Source starts the effect and immediately completes the held action.
                        // Native marker lifetime uses trainer_see.c's 4+4+52-frame animation.
                        a.emote = 60;
                        t.pc += 1;
                        continue;
                    }
                    if next == 0x60 {
                        a.visible = false;
                    } else if next == 0x61 {
                        a.visible = true;
                    }
                }
                if step(t, data, &mut a.at, &mut a.facing, a.original, game.position)? {
                    a.task = None;
                }
            }
        }
        Ok(())
    }
}
fn display(at: Cell, task: Option<&Task>) -> [f32; 2] {
    if let Some(t) = task.filter(|t| t.duration > 0 && t.remaining > 0) {
        let p = 1. - f32::from(t.remaining) / f32::from(t.duration);
        [
            t.from.0 as f32 + (t.to.0 - t.from.0) as f32 * p,
            t.from.1 as f32 + (t.to.1 - t.from.1) as f32 * p,
        ]
    } else {
        [at.0 as f32, at.1 as f32]
    }
}
fn step(
    t: &mut Task,
    data: &Scripts,
    at: &mut Cell,
    face: &mut Cell,
    original: Cell,
    player: Cell,
) -> Result<bool, String> {
    if t.remaining > 0 {
        t.remaining -= 1;
        if t.remaining == 0 {
            *at = t.to;
        }
        return Ok(false);
    }
    let action = byte(data, t.pc)?;
    t.pc += 1;
    if action == 0xfe {
        return Ok(true);
    }
    t.from = *at;
    t.to = *at;
    let (dir, frames, walk) = match action {
        0x00..=0x07 => (Some(action % 4), 1, false),
        0x08..=0x0b => (Some(action - 0x08), 31, true),
        0x0c..=0x0f => (Some(action - 0x0c), 23, true),
        0x9b..=0x9e => (Some(action - 0x9b), 160, true),
        0x10..=0x13 => (Some(action - 0x10), 16, true),
        0x18..=0x1c => (None, 1 << (action - 0x18), false),
        0x1d..=0x20 => (Some(action - 0x1d), 8, true),
        0x21..=0x24 => (Some(action - 0x21), 32, false),
        0x25..=0x28 => (Some(action - 0x25), 16, false),
        0x29..=0x2c => (Some(action - 0x29), 8, false),
        0x2d..=0x30 => (Some(action - 0x2d), 4, false),
        0x35..=0x38 => (Some(action - 0x35), 4, true),
        0x4a | 0x4b => {
            let dx = player.0 - at.0;
            let dy = player.1 - at.1;
            *face = if dx.abs() > dy.abs() {
                Cell(dx.signum(), 0)
            } else {
                Cell(0, if dy == 0 { 1 } else { dy.signum() })
            };
            if action == 0x4b {
                *face = Cell(-face.0, -face.1);
            }
            (None, 1, false)
        }
        0x5a => {
            *face = original;
            (None, 1, false)
        }
        0x5b => (Some(0), 16, false),
        0x60 | 0x61 | 0x67 => (None, 1, false),
        _ => return Err(format!("Unported original movement action {action:#x}")),
    };
    if let Some(d) = dir {
        *face = direction(d + 1)?;
        if walk {
            t.to = at.offset(*face);
        }
    }
    t.duration = frames;
    t.remaining = frames;
    Ok(false)
}

/// Bounded native startup integration, not original-C parity verification.
pub fn check(world: &World) -> Result<(), String> {
    let data = Data::source();
    let scripts = Scripts::source();
    let core = crate::core::Data::source();
    if data.revision != scripts.revision || data.revision != core.revision {
        return Err("Actor revision mismatch".into());
    }
    let mut state = State::new(&core, 42, 1234);
    let mut reset = crate::field::Vm::start(&scripts, "EventScript_ResetAllMapFlags")?;
    if reset.run(&scripts, &core, &mut state, 1024)? != crate::field::Step::End {
        return Err("Map reset failed".into());
    }
    let mut actors = Actors::default();
    actors.load(&data, "pallet", &state, &[])?;
    if actors
        .actors
        .iter()
        .any(|a| a.object.local_id == 3 && a.present)
    {
        return Err("Original hidden actor flag failed".into());
    }
    if data.table(&scripts, "pallet", 3, &state)?.is_none() {
        return Err("Map transition table missing".into());
    }
    let mut game = Game::new(world, "pallet");
    let start = actors.actors[0].at;
    actors.apply(
        1,
        scripts.label("PalletTown_Movement_SignLadyMoveOutOfWayRight")?,
        &game,
    );
    // Original scheduler refuses a replacement while this object is still moving.
    actors.apply(
        1,
        scripts.label("PalletTown_Movement_SignLadyMoveOutOfWayLeft")?,
        &game,
    );
    actors.apply(
        0xff,
        scripts.label("Common_Movement_WalkInPlaceFasterLeft")?,
        &game,
    );
    let mut parallel = false;
    for _ in 0..80 {
        actors.tick(&scripts, &mut game)?;
        if actors.finished(0) && actors.busy() {
            parallel = true;
        }
    }
    if !parallel
        || actors.busy()
        || actors.actors[0].at != start.offset(crate::EAST)
        || game.facing != crate::WEST
    {
        return Err("Original movement queue/parallel wait integration failed".into());
    }
    let saved = actors.templates();
    let mut restored = Actors::default();
    restored.load(&data, "pallet", &state, &saved)?;
    if restored.templates() != saved {
        return Err("Actor template restore failed".into());
    }
    Ok(())
}
