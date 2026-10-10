//! Native simulation derived from pinned pret/pokefirered; no renderer or guest runtime.
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
pub mod actors;
pub mod battle;
pub mod core;
pub mod encounter;
pub mod field;
pub mod free;
pub const TICK_HZ: f64 = 16_777_216.0 / 280_896.0;
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct Cell(pub i32, pub i32);
impl Cell {
    pub fn offset(self, d: Cell) -> Self {
        Self(self.0 + d.0, self.1 + d.1)
    }
}
pub const SOUTH: Cell = Cell(0, 1);
pub const NORTH: Cell = Cell(0, -1);
pub const WEST: Cell = Cell(-1, 0);
pub const EAST: Cell = Cell(1, 0);
#[derive(Clone, Debug, Deserialize)]
pub struct Prop {
    pub kind: String,
    pub at: Cell,
    pub size: Cell,
    #[serde(default)]
    pub door: Option<Cell>,
    #[serde(default)]
    pub solid: bool,
    #[serde(default)]
    pub label: String,
}
#[derive(Clone, Debug, Deserialize)]
pub struct Npc {
    #[serde(default)]
    pub local_id: u16,
    #[serde(default)]
    pub invisible: bool,
    pub at: Cell,
    pub name: String,
    pub lines: Vec<String>,
    pub script: String,
    pub elevation: u8,
    pub movement: String,
    pub graphics: String,
}
#[derive(Clone, Debug, Deserialize)]
pub struct Sign {
    pub at: Cell,
    pub lines: Vec<String>,
    pub script: String,
}
#[derive(Clone, Debug, Deserialize)]
pub struct Warp {
    pub at: Cell,
    pub map: String,
    pub to: Cell,
    pub source_index: usize,
}
#[derive(Clone, Debug, Deserialize)]
pub struct Connection {
    pub direction: String,
    pub offset: i32,
    pub map: String,
}
#[derive(Clone, Debug, Deserialize)]
pub struct Map {
    pub id: String,
    pub name: String,
    pub spawn: Cell,
    pub tiles: Vec<String>,
    pub cells: Vec<u16>,
    pub behaviors: Vec<u16>,
    pub encounter_types: Vec<u8>,
    pub indoor: bool,
    pub allow_running: bool,
    pub props: Vec<Prop>,
    pub npcs: Vec<Npc>,
    pub signs: Vec<Sign>,
    pub warps: Vec<Warp>,
    pub connections: Vec<Connection>,
    pub source: String,
}
#[derive(Clone, Debug, Deserialize)]
pub struct World {
    pub maps: Vec<Map>,
}
impl World {
    pub fn load(path: &std::path::Path) -> Result<Self, String> {
        let w: Self =
            serde_json::from_str(&std::fs::read_to_string(path).map_err(|e| e.to_string())?)
                .map_err(|e| e.to_string())?;
        w.validate()?;
        Ok(w)
    }
    pub fn map(&self, id: &str) -> &Map {
        self.maps
            .iter()
            .find(|m| m.id == id)
            .expect("validated map")
    }
    pub fn validate(&self) -> Result<(), String> {
        if self.maps.is_empty() {
            return Err("No maps".into());
        }
        let mut ids = HashSet::new();
        for m in &self.maps {
            if !ids.insert(&m.id)
                || m.tiles.is_empty()
                || m.tiles[0].is_empty()
                || m.tiles
                    .iter()
                    .any(|r| r.len() != m.tiles[0].len() || !r.is_ascii())
                || m.cells.len() != m.width() * m.height()
                || m.behaviors.len() != m.cells.len()
                || m.encounter_types.len() != m.cells.len()
                || m.encounter_types.iter().any(|&kind| kind > 2)
            {
                return Err(format!("Invalid map {}", m.id));
            }
            if !m.terrain_walkable(m.spawn) {
                return Err(format!("Blocked spawn {} {:?}", m.id, m.spawn));
            }
            for n in &m.npcs {
                if !m.in_bounds(n.at) {
                    return Err("NPC out of bounds".into());
                }
            }
        }
        for m in &self.maps {
            for w in &m.warps {
                let target = self
                    .maps
                    .iter()
                    .find(|v| v.id == w.map)
                    .ok_or("Missing warp map")?;
                if !m.in_bounds(w.at) || !target.in_bounds(w.to) {
                    return Err("Invalid warp".into());
                }
            }
            for c in &m.connections {
                if !ids.contains(&c.map)
                    || !["up", "down", "left", "right"].contains(&c.direction.as_str())
                {
                    return Err("Invalid connection".into());
                }
            }
        }
        Ok(())
    }
    pub fn destination(&self, id: &str, p: Cell) -> Option<(String, Cell)> {
        let m = self.map(id);
        if m.in_bounds(p) {
            return Some((id.into(), p));
        }
        let direction = if p.1 < 0 {
            "up"
        } else if p.1 >= m.height() as i32 {
            "down"
        } else if p.0 < 0 {
            "left"
        } else {
            "right"
        };
        let c = m.connections.iter().find(|c| c.direction == direction)?;
        let target = self.map(&c.map);
        let p = match direction {
            "up" => Cell(p.0 - c.offset, target.height() as i32 + p.1),
            "down" => Cell(p.0 - c.offset, p.1 - m.height() as i32),
            "left" => Cell(target.width() as i32 + p.0, p.1 - c.offset),
            _ => Cell(p.0 - m.width() as i32, p.1 - c.offset),
        };
        target.in_bounds(p).then_some((c.map.clone(), p))
    }
}
impl Map {
    pub fn encounter_type(&self, p: Cell) -> u8 {
        if self.in_bounds(p) {
            self.encounter_types[self.index(p)]
        } else {
            0
        }
    }
    pub fn width(&self) -> usize {
        self.tiles[0].len()
    }
    pub fn height(&self) -> usize {
        self.tiles.len()
    }
    pub fn in_bounds(&self, p: Cell) -> bool {
        p.0 >= 0 && p.1 >= 0 && (p.0 as usize) < self.width() && (p.1 as usize) < self.height()
    }
    fn index(&self, p: Cell) -> usize {
        p.1 as usize * self.width() + p.0 as usize
    }
    pub fn cell(&self, p: Cell) -> u16 {
        if self.in_bounds(p) {
            self.cells[self.index(p)]
        } else {
            0xffff
        }
    }
    pub fn behavior(&self, p: Cell) -> u16 {
        if self.in_bounds(p) {
            self.behaviors[self.index(p)]
        } else {
            0
        }
    }
    pub fn elevation(&self, p: Cell) -> u8 {
        (self.cell(p) >> 12) as u8
    }
    pub fn terrain_walkable(&self, p: Cell) -> bool {
        self.in_bounds(p) && ((self.cell(p) >> 10) & 3) == 0
    }
    pub fn walkable(&self, p: Cell) -> bool {
        self.terrain_walkable(p) && !self.npcs.iter().any(|n| n.at == p)
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Collision {
    None,
    Boundary,
    Impassable,
    Elevation,
    Npc,
    Ledge,
}
fn directional_block(b: u16, d: Cell) -> bool {
    match d {
        EAST => [0x30, 0x34, 0x36].contains(&b),
        WEST => [0x31, 0x35, 0x37].contains(&b),
        NORTH => [0x32, 0x34, 0x35].contains(&b),
        SOUTH => [0x33, 0x36, 0x37].contains(&b),
        _ => true,
    }
}
fn ledge(b: u16, d: Cell) -> bool {
    matches!(
        (b, d),
        (0x38, EAST) | (0x39, WEST) | (0x3a, NORTH) | (0x3b, SOUTH)
    )
}
/// PC extension: both routes around a diagonal must be clear, preventing corner cutting.
fn diagonal_collision(world: &World, id: &str, p: Cell, elevation: u8, d: Cell) -> Collision {
    let Some(end) = world.destination(id, p.offset(d)) else {
        return Collision::Boundary;
    };
    for first in [Cell(d.0, 0), Cell(0, d.1)] {
        let result = collision(world, id, p, elevation, first);
        if result != Collision::None {
            return if result == Collision::Ledge {
                Collision::Impassable
            } else {
                result
            };
        }
        let Some((map, q)) = world.destination(id, p.offset(first)) else {
            return Collision::Boundary;
        };
        let m = world.map(&map);
        // Doors, stairs and ledges retain cardinal entry semantics.
        if m.warps.iter().any(|w| w.at == q) {
            return Collision::Impassable;
        }
        let second = Cell(d.0 - first.0, d.1 - first.1);
        let result = collision(world, &map, q, elevation, second);
        if result != Collision::None {
            return if result == Collision::Ledge {
                Collision::Impassable
            } else {
                result
            };
        }
        if world.destination(&map, q.offset(second)) != Some(end.clone()) {
            return Collision::Boundary;
        }
    }
    if world.map(&end.0).warps.iter().any(|w| w.at == end.1) {
        return Collision::Impassable;
    }
    Collision::None
}
/// Camera-relative keyboard intent, quantized to four or eight directions.
pub fn camera_direction(x: i32, z: i32, yaw: f32, diagonal: bool) -> Option<Cell> {
    if x == 0 && z == 0 {
        return None;
    }
    let (sn, cs) = yaw.sin_cos();
    let (x, z) = (
        x as f32 * cs + z as f32 * sn,
        -x as f32 * sn + z as f32 * cs,
    );
    if diagonal {
        let angle =
            (x.atan2(z) / std::f32::consts::FRAC_PI_4).round() * std::f32::consts::FRAC_PI_4;
        Some(Cell(angle.sin().round() as i32, angle.cos().round() as i32))
    } else if x.abs() > z.abs() {
        Some(Cell(if x > 0. { 1 } else { -1 }, 0))
    } else {
        Some(Cell(0, if z > 0. { 1 } else { -1 }))
    }
}
pub fn collision(world: &World, id: &str, p: Cell, elevation: u8, d: Cell) -> Collision {
    if d.0.abs() == 1 && d.1.abs() == 1 {
        return diagonal_collision(world, id, p, elevation, d);
    }

    let Some((target, q)) = world.destination(id, p.offset(d)) else {
        return Collision::Boundary;
    };
    let m = world.map(&target);
    let current = world.map(id);
    // CheckForObjectEventCollision: ledge behaviour overrides the ordinary solid tile bit.
    if ledge(m.behavior(q), d) {
        return Collision::Ledge;
    }
    // TryDoorWarp is handled before ordinary collision in field_control_avatar.c.
    if d == NORTH && m.behavior(q) == 0x69 && m.warps.iter().any(|w| w.at == q) {
        return Collision::None;
    }
    if !m.terrain_walkable(q)
        || directional_block(current.behavior(p), d)
        || directional_block(m.behavior(q), Cell(-d.0, -d.1))
    {
        return Collision::Impassable;
    }
    let e = m.elevation(q);
    if elevation != 0 && e != 0 && e != 15 && e != elevation {
        return Collision::Elevation;
    }
    if m.npcs
        .iter()
        .any(|n| n.at == q && (elevation == 0 || n.elevation == 0 || n.elevation == elevation))
    {
        return Collision::Npc;
    }
    Collision::None
}
#[derive(Clone, Copy, Default)]
pub struct Input {
    pub direction: Option<Cell>,
    pub run: bool,
    pub interact: bool,
}
#[derive(Clone, Debug)]
pub struct Motion {
    pub from: Cell,
    pub to: Cell,
    pub target_map: String,
    pub ticks: u8,
    pub duration: u8,
    pub jump: bool,
}
impl Motion {
    pub fn progress(&self) -> f32 {
        self.ticks as f32 / self.duration as f32
    }
}
#[derive(Clone, Debug)]
pub struct Game {
    pub map: String,
    pub position: Cell,
    pub facing: Cell,
    pub elevation: u8,
    pub dialogue: Option<(String, Vec<String>, usize)>,
    pub motion: Option<Motion>,
    pub running_shoes: bool,
    pub field_scripts: bool,
    pub script_locked: bool,
    pub pending_script: Option<(String, String)>,
    pub allow_diagonal: bool,
    pub flags: HashSet<String>,
    pub inventory: HashMap<String, u16>,
    pub ticks: u64,
    pub last_collision: Collision,
    turn_ticks: u8,
    continuous: bool,
    pending_potion: bool,
}
impl Game {
    pub fn new(world: &World, id: &str) -> Self {
        let position = world.map(id).spawn;
        Self {
            map: id.into(),
            position,
            facing: SOUTH,
            elevation: world.map(id).elevation(position),
            dialogue: None,
            motion: None,
            running_shoes: true,
            field_scripts: false,
            script_locked: false,
            pending_script: None,
            allow_diagonal: false,
            flags: HashSet::new(),
            inventory: HashMap::new(),
            ticks: 0,
            last_collision: Collision::None,
            turn_ticks: 0,
            continuous: false,
            pending_potion: false,
        }
    }
    pub fn busy(&self) -> bool {
        self.motion.is_some() || self.turn_ticks > 0 || self.script_locked
    }
    pub fn tick(&mut self, world: &World, input: Input) {
        self.ticks += 1;
        if let Some(m) = &mut self.motion {
            m.ticks += 1;
            if m.ticks >= m.duration {
                let m = self.motion.take().unwrap();
                self.map = m.target_map;
                self.position = m.to;
                self.update_elevation(world);
                self.arrive(world);
            }
            return;
        }
        if self.turn_ticks > 0 {
            self.turn_ticks -= 1;
            return;
        }
        if input.interact {
            self.interact(world);
            return;
        }
        if self.script_locked {
            return;
        }
        if self.dialogue.is_some() {
            return;
        }
        let Some(d) = input.direction else {
            self.continuous = false;
            return;
        };
        let diagonal = d.0.abs() == 1 && d.1.abs() == 1;
        if ![NORTH, SOUTH, EAST, WEST].contains(&d) && !(self.allow_diagonal && diagonal) {
            return;
        }
        // CheckMovementInputNotOnBike: a fresh direction turns in place; continuous movement can turn directly.
        if d != self.facing && !self.continuous {
            self.facing = d;
            self.turn_ticks = 7;
            return;
        }
        self.facing = d;
        self.last_collision = collision(world, &self.map, self.position, self.elevation, d);
        let distance = if self.last_collision == Collision::Ledge {
            2
        } else {
            1
        };
        if ![Collision::None, Collision::Ledge].contains(&self.last_collision) {
            self.continuous = false;
            return;
        }
        let Some((target, to)) = world.destination(
            &self.map,
            self.position.offset(Cell(d.0 * distance, d.1 * distance)),
        ) else {
            return;
        };
        if distance == 2 && !world.map(&target).walkable(to) {
            return;
        }
        let running = input.run
            && self.running_shoes
            && world.map(&self.map).allow_running
            && world.map(&self.map).behavior(self.position) != 0x0a;
        self.motion = Some(Motion {
            from: self.position,
            to,
            target_map: target,
            ticks: 0,
            duration: if distance == 2 {
                32
            } else if diagonal {
                if running { 11 } else { 23 }
            } else if running {
                8
            } else {
                16
            },
            jump: distance == 2,
        });
        self.continuous = true;
    }
    fn update_elevation(&mut self, world: &World) {
        let e = world.map(&self.map).elevation(self.position);
        if e != 0 && e != 15 {
            self.elevation = e;
        }
    }
    fn arrive(&mut self, world: &World) {
        if let Some(w) = world
            .map(&self.map)
            .warps
            .iter()
            .find(|w| w.at == self.position)
        {
            self.map = w.map.clone();
            self.position = w.to;
            // Clear door thresholds; stair destinations stay on the original warp coordinate.
            let m = world.map(&self.map);
            let d = if m.behavior(self.position) == 0x69 {
                Some(SOUTH)
            } else if [0x65, 0x67].contains(&m.behavior(self.position)) {
                Some(NORTH)
            } else {
                None
            };
            if let Some(d) = d {
                if m.terrain_walkable(self.position.offset(d)) {
                    self.position = self.position.offset(d);
                    self.facing = d;
                }
            }
            self.update_elevation(world);
            self.continuous = false;
        }
    }
    pub fn interact(&mut self, world: &World) {
        if self.busy() {
            return;
        }
        if let Some((_, lines, i)) = &mut self.dialogue {
            *i += 1;
            if *i == lines.len() {
                self.dialogue = None;
                if self.pending_potion {
                    *self.inventory.entry("ITEM_POTION".into()).or_default() += 1;
                    self.flags.insert("FLAG_GOT_POTION_ON_ROUTE_1".into());
                    self.pending_potion = false;
                }
            }
            return;
        }
        let m = world.map(&self.map);
        let q = self.position.offset(self.facing);
        if self.facing.0 != 0
            && self.facing.1 != 0
            && (!m.walkable(self.position.offset(Cell(self.facing.0, 0)))
                || !m.walkable(self.position.offset(Cell(0, self.facing.1))))
        {
            return;
        }
        self.interact_at(world, q);
    }
    pub fn interact_at(&mut self, world: &World, q: Cell) {
        if self.busy() || self.dialogue.is_some() {
            return;
        }
        let m = world.map(&self.map);
        if let Some(n) = m.npcs.iter().find(|n| n.at == q) {
            if self.field_scripts {
                self.pending_script = Some((n.script.clone(), n.name.clone()));
                self.script_locked = true;
                return;
            }
            let mut lines = n.lines.clone();
            // Native port of Route1_EventScript_MartClerk's one-time item/flag branch.
            if n.script == "Route1_EventScript_MartClerk" {
                if self.flags.contains("FLAG_GOT_POTION_ON_ROUTE_1") {
                    lines = vec![
                        "Please come see us if you need\nPOKé BALLS for catching POKéMON.".into(),
                    ];
                } else {
                    lines.push("RED obtained the POTION!".into());
                    lines.push("RED put the POTION away in\nthe BAG's ITEMS POCKET.".into());
                    self.pending_potion = true;
                }
            }
            if lines.is_empty() {
                lines.push("This interaction is not available in this slice yet.".into());
            }
            self.dialogue = Some((n.name.clone(), lines, 0));
        } else if let Some(s) = m.signs.iter().find(|s| s.at == q) {
            if self.field_scripts {
                self.pending_script = Some((s.script.clone(), String::new()));
                self.script_locked = true;
                return;
            }
            let lines = if s.lines.is_empty() {
                vec!["This interaction is not available in this slice yet.".into()]
            } else {
                s.lines.clone()
            };
            self.dialogue = Some(("".into(), lines, 0));
        }
    }
    pub fn cancel_dialogue(&mut self) {
        self.dialogue = None;
        self.pending_potion = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn world() -> World {
        World::load(std::path::Path::new("data/world.json")).unwrap()
    }
    fn complete(g: &mut Game, w: &World, d: Cell, run: bool) {
        g.facing = d;
        g.tick(
            w,
            Input {
                direction: Some(d),
                run,
                interact: false,
            },
        );
        for _ in 0..40 {
            if !g.busy() {
                break;
            }
            g.tick(w, Input::default());
        }
    }
    #[test]
    fn dimensions_and_doors() {
        let w = world();
        assert_eq!(
            (w.map("pallet").width(), w.map("pallet").height()),
            (24, 20)
        );
        assert_eq!(
            (w.map("route1").width(), w.map("route1").height()),
            (24, 40)
        );
        assert_eq!(w.map("pallet").warps[0].at, Cell(6, 7));
        assert_eq!(w.map("home").width(), 13);
    }
    #[test]
    fn turning_does_not_move() {
        let w = world();
        let mut g = Game::new(&w, "pallet");
        g.tick(
            &w,
            Input {
                direction: Some(EAST),
                ..Default::default()
            },
        );
        assert_eq!(g.position, Cell(10, 10));
        assert!(g.motion.is_none());
        assert_eq!(g.facing, EAST);
    }
    #[test]
    fn completes_without_renderer() {
        let w = world();
        let mut g = Game::new(&w, "pallet");
        let p = g.position;
        complete(&mut g, &w, EAST, false);
        assert_eq!(g.position, p.offset(EAST));
    }
    #[test]
    fn source_walk_and_run_durations() {
        let w = world();
        for (run, ticks) in [(false, 16), (true, 8)] {
            let mut g = Game::new(&w, "pallet");
            g.facing = EAST;
            g.tick(
                &w,
                Input {
                    direction: Some(EAST),
                    run,
                    interact: false,
                },
            );
            assert_eq!(g.motion.unwrap().duration, ticks);
        }
    }
    #[test]
    fn running_restrictions() {
        let w = world();
        let mut g = Game::new(&w, "home");
        g.facing = NORTH;
        g.tick(
            &w,
            Input {
                direction: Some(NORTH),
                run: true,
                interact: false,
            },
        );
        assert_eq!(g.motion.unwrap().duration, 16);
        let mut g = Game::new(&w, "pallet");
        g.running_shoes = false;
        g.facing = EAST;
        g.tick(
            &w,
            Input {
                direction: Some(EAST),
                run: true,
                interact: false,
            },
        );
        assert_eq!(g.motion.unwrap().duration, 16);
    }
    #[test]
    fn elevation_rules() {
        let w = world();
        assert_eq!(
            collision(&w, "pallet", Cell(10, 10), 4, EAST),
            Collision::Elevation
        );
        assert_eq!(
            collision(&w, "pallet", Cell(10, 10), 0, EAST),
            Collision::None
        );
    }
    #[test]
    fn route_round_trip() {
        let w = world();
        let mut g = Game::new(&w, "pallet");
        g.position = Cell(12, 0);
        complete(&mut g, &w, NORTH, false);
        assert_eq!((&*g.map, g.position), ("route1", Cell(12, 39)));
        complete(&mut g, &w, SOUTH, false);
        assert_eq!((&*g.map, g.position), ("pallet", Cell(12, 0)));
    }
    #[test]
    fn house_round_trip() {
        let w = world();
        let mut g = Game::new(&w, "pallet");
        g.position = Cell(6, 8);
        complete(&mut g, &w, NORTH, false);
        assert_eq!((&*g.map, g.position), ("home", Cell(4, 7)));
        complete(&mut g, &w, SOUTH, false);
        assert_eq!((&*g.map, g.position), ("pallet", Cell(6, 8)));
    }
    #[test]
    fn source_ledges() {
        let w = world();
        let m = w.map("route1");
        let mut count = 0;
        for y in 1..m.height() - 2 {
            for x in 1..m.width() - 1 {
                let p = Cell(x as i32, y as i32);
                if m.behavior(p) == 0x3b
                    && m.walkable(p.offset(NORTH))
                    && m.walkable(p.offset(SOUTH))
                {
                    let mut g = Game::new(&w, "route1");
                    g.position = p.offset(NORTH);
                    complete(&mut g, &w, SOUTH, false);
                    assert_eq!(g.position, p.offset(SOUTH));
                    assert_ne!(
                        collision(&w, "route1", g.position, 3, NORTH),
                        Collision::Ledge
                    );
                    count += 1;
                }
            }
        }
        assert!(count > 0);
    }
    #[test]
    fn one_potion_and_dialogue_lock() {
        let w = world();
        let mut g = Game::new(&w, "route1");
        g.position = Cell(6, 29);
        g.facing = NORTH;
        g.interact(&w);
        assert!(g.dialogue.is_some());
        g.tick(
            &w,
            Input {
                direction: Some(EAST),
                ..Default::default()
            },
        );
        assert!(g.motion.is_none());
        while g.dialogue.is_some() {
            g.interact(&w);
        }
        assert_eq!(g.inventory["ITEM_POTION"], 1);
        g.interact(&w);
        while g.dialogue.is_some() {
            g.interact(&w);
        }
        assert_eq!(g.inventory["ITEM_POTION"], 1);
    }
    #[test]
    fn bounded_world() {
        let w = world();
        // Route 1 connects to Viridian City to the north
        assert_eq!(w.destination("route1", Cell(12, -1)).unwrap().0, "viridian");
        // Viridian City connects to Route 2 to the north and Route 22 to the west
        assert_eq!(w.destination("viridian", Cell(21, -1)).unwrap().0, "route2");
        assert_eq!(
            w.destination("viridian", Cell(-1, 26)).unwrap().0,
            "route22"
        );
        // Route 2 connects to Pewter City to the north
        assert_eq!(w.destination("route2", Cell(15, -1)).unwrap().0, "pewter");
        // West of Route 1 has no map connection
        assert!(w.destination("route1", Cell(-1, 20)).is_none());
        assert!(w.destination("pallet", Cell(12, 20)).is_none());
    }
    #[test]
    fn collision_is_independent_of_art() {
        let w = world();
        let mut m = w.map("pallet").clone();
        m.props.clear();
        assert!(!m.terrain_walkable(Cell(5, 5)));
    }
    #[test]
    fn missing_connection_rejected() {
        let mut w = world();
        w.maps[0].connections[0].map = "missing".into();
        assert!(w.validate().is_err());
    }
    fn open_world() -> World {
        let mut w = world();
        for m in &mut w.maps {
            m.cells.fill(0x3000);
            m.behaviors.fill(0);
            m.npcs.clear();
            m.warps.clear();
        }
        w
    }
    #[test]
    fn diagonal_speed_and_original_mode() {
        let w = open_world();
        for d in [Cell(1, 1), Cell(1, -1), Cell(-1, 1), Cell(-1, -1)] {
            for (run, duration) in [(false, 23), (true, 11)] {
                let mut g = Game::new(&w, "pallet");
                g.allow_diagonal = true;
                g.facing = d;
                g.tick(
                    &w,
                    Input {
                        direction: Some(d),
                        run,
                        interact: false,
                    },
                );
                assert_eq!(g.motion.as_ref().unwrap().duration, duration);
                let origin = g.position;
                while g.busy() {
                    g.tick(&w, Input::default());
                }
                assert_eq!(g.position, origin.offset(d));
            }
        }
        let mut g = Game::new(&w, "pallet");
        complete(&mut g, &w, Cell(1, -1), false);
        assert_eq!(g.position, Cell(10, 10));
    }
    #[test]
    fn diagonal_cannot_cut_corners_or_bypass_elevation() {
        for q in [Cell(11, 10), Cell(10, 9), Cell(11, 9)] {
            let mut w = open_world();
            let m = &mut w.maps[0];
            let i = m.index(q);
            m.cells[i] |= 0x400;
            let mut g = Game::new(&w, "pallet");
            g.allow_diagonal = true;
            complete(&mut g, &w, Cell(1, -1), false);
            assert_eq!(g.position, Cell(10, 10));
        }
        let mut w = open_world();
        let m = &mut w.maps[0];
        let i = m.index(Cell(11, 9));
        m.cells[i] = 0x4000;
        assert_eq!(
            collision(&w, "pallet", Cell(10, 10), 3, Cell(1, -1)),
            Collision::Elevation
        );
    }
    #[test]
    fn diagonal_npc_interaction_and_dialogue_lock() {
        let mut w = open_world();
        let mut npc = world().map("pallet").npcs[0].clone();
        npc.at = Cell(11, 9);
        npc.elevation = 3;
        w.maps[0].npcs.push(npc);
        assert_eq!(
            collision(&w, "pallet", Cell(10, 10), 3, Cell(1, -1)),
            Collision::Npc
        );
        let mut g = Game::new(&w, "pallet");
        g.allow_diagonal = true;
        g.facing = Cell(1, -1);
        g.interact(&w);
        assert!(g.dialogue.is_some());
        g.tick(
            &w,
            Input {
                direction: Some(Cell(-1, 1)),
                ..Default::default()
            },
        );
        assert!(!g.busy());
        g.cancel_dialogue();
        let i = w.maps[0].index(Cell(11, 10));
        w.maps[0].cells[i] |= 0x400;
        g.interact(&w);
        assert!(g.dialogue.is_none());
        // A side NPC also stops diagonal passage.
        w.maps[0].cells[i] = 0x3000;
        w.maps[0].npcs[0].at = Cell(11, 10);
        assert_eq!(
            collision(&w, "pallet", Cell(10, 10), 3, Cell(1, -1)),
            Collision::Npc
        );
    }
    #[test]
    fn diagonal_preserves_cardinal_events_and_map_connections() {
        let mut w = open_world();
        let i = w.maps[0].index(Cell(10, 11));
        w.maps[0].behaviors[i] = 0x3b;
        assert_eq!(
            collision(&w, "pallet", Cell(10, 10), 3, Cell(1, 1)),
            Collision::Impassable
        );
        w.maps[0].behaviors[i] = 0;
        w.maps[0].warps.push(Warp {
            at: Cell(11, 11),
            map: "home".into(),
            to: Cell(4, 7),
            source_index: 0,
        });
        let mut g = Game::new(&w, "pallet");
        g.allow_diagonal = true;
        complete(&mut g, &w, Cell(1, 1), false);
        assert_eq!(g.map, "pallet");
        assert_eq!(g.position, Cell(10, 10));
        complete(&mut g, &w, EAST, false);
        complete(&mut g, &w, SOUTH, false);
        assert_eq!(g.map, "home");
        let w = open_world();
        let mut g = Game::new(&w, "pallet");
        g.allow_diagonal = true;
        g.position = Cell(12, 0);
        complete(&mut g, &w, Cell(1, -1), false);
        assert_eq!((g.map.as_str(), g.position), ("route1", Cell(13, 39)));
    }
    #[test]
    fn camera_intent_supports_diagonals_and_cancels_opposites() {
        assert_eq!(camera_direction(1, -1, 0., true), Some(Cell(1, -1)));
        assert_eq!(
            camera_direction(0, -1, std::f32::consts::FRAC_PI_2, true),
            Some(WEST)
        );
        assert_eq!(
            camera_direction(0, -1, std::f32::consts::FRAC_PI_4, true),
            Some(Cell(-1, -1))
        );
        assert_eq!(camera_direction(0, 0, 1., true), None);
        let d = camera_direction(1, -1, 0., false).unwrap();
        assert!([NORTH, SOUTH, EAST, WEST].contains(&d));
    }
}
