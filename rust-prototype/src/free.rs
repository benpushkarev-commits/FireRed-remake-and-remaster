//! Continuous PC controller. Imported cells still define collision and event locations.
use super::*;

pub const RADIUS: f32 = 0.22;
pub fn camera_vector(x: i32, z: i32, yaw: f32) -> [f32; 2] {
    let length = ((x * x + z * z) as f32).sqrt();
    if length == 0. {
        return [0., 0.];
    }
    let (sn, cs) = yaw.sin_cos();
    [
        (x as f32 * cs + z as f32 * sn) / length,
        (-x as f32 * sn + z as f32 * cs) / length,
    ]
}

#[cfg(test)]
mod tests {
    use super::*;
    fn world() -> World {
        World::load(std::path::Path::new("data/world.json")).unwrap()
    }
    fn open() -> World {
        let mut w = world();
        for m in &mut w.maps {
            m.cells.fill(0x3000);
            m.behaviors.fill(0);
            m.npcs.clear();
            m.warps.clear();
        }
        w
    }
    fn advance(c: &mut Controller, g: &mut Game, w: &World, v: [f32; 2], ticks: usize) {
        for _ in 0..ticks {
            c.tick(g, w, v, false, false);
        }
    }
    #[test]
    fn arbitrary_angle_is_continuous_and_speed_normalized() {
        let w = open();
        let mut g = Game::new(&w, "pallet");
        let mut c = Controller::new(&g);
        advance(&mut c, &mut g, &w, camera_vector(0, -1, 0.31), 8);
        assert!((distance(c.position, [10., 10.]) - 0.5).abs() < 0.002);
        assert!(c.position[0].fract().abs() > 0.1);
        assert!(g.motion.is_none());
        let end = c.position;
        advance(&mut c, &mut g, &w, [0., 0.], 16);
        assert_eq!(end, c.position);
        for v in [[1., 0.], [1., 1.]] {
            let mut g = Game::new(&w, "pallet");
            let mut c = Controller::new(&g);
            advance(&mut c, &mut g, &w, v, 8);
            assert!((distance(c.position, [10., 10.]) - 0.5).abs() < 0.002);
        }
    }
    #[test]
    fn radius_collision_slides_without_corner_cutting_or_tunnelling() {
        let mut w = open();
        for y in 0..20 {
            let i = w.maps[0].index(Cell(11, y));
            w.maps[0].cells[i] |= 0x400;
        }
        let mut g = Game::new(&w, "pallet");
        let mut c = Controller::new(&g);
        for _ in 0..50 {
            c.tick(&mut g, &w, [1., -1.], true, false);
            assert!(clear(&w, &g.map, c.position, g.elevation));
        }
        assert!(c.position[0] <= 10.281);
        assert!(c.position[1] < 8.);
        let i = w.maps[0].index(Cell(10, 9));
        w.maps[0].cells[i] |= 0x400;
        let mut g = Game::new(&w, "pallet");
        let mut c = Controller::new(&g);
        advance(&mut c, &mut g, &w, [1., -1.], 80);
        assert!(c.position[0] < 10.3 && c.position[1] > 9.7);
    }
    #[test]
    fn doors_stairs_and_connections_survive_continuous_motion() {
        let w = world();
        let mut g = Game::new(&w, "pallet");
        g.position = Cell(6, 8);
        let mut c = Controller::new(&g);
        for _ in 0..30 {
            c.tick(&mut g, &w, [0., -1.], false, false);
            if g.map == "home" {
                break;
            }
        }
        assert_eq!((g.map.as_str(), g.position), ("home", Cell(4, 7)));
        for _ in 0..35 {
            c.tick(&mut g, &w, [0., 1.], false, false);
            if g.map == "pallet" {
                break;
            }
        }
        assert_eq!((g.map.as_str(), g.position), ("pallet", Cell(6, 8)));
        let w = open();
        let mut g = Game::new(&w, "pallet");
        g.position = Cell(12, 0);
        let mut c = Controller::new(&g);
        c.position[0] = 12.17;
        advance(&mut c, &mut g, &w, [0., -1.], 10);
        assert_eq!(g.map, "route1");
        assert!((c.position[0] - 12.17).abs() < 0.001);
        advance(&mut c, &mut g, &w, [0., 1.], 10);
        assert_eq!(g.map, "pallet");
        assert!((c.position[1]).abs() < 0.001);
        let w = world();
        let mut g = Game::new(&w, "home");
        g.position = Cell(10, 3);
        let mut c = Controller::new(&g);
        c.warp_ready = true;
        advance(&mut c, &mut g, &w, [0., -1.], 16);
        assert_eq!(g.map, "bedroom");
        advance(&mut c, &mut g, &w, [0., 0.], 120);
        assert_eq!(g.map, "bedroom");
    }
    #[test]
    fn npc_body_and_fractional_interaction_lock_movement() {
        let mut w = open();
        let mut n = world().map("pallet").npcs[0].clone();
        n.at = Cell(10, 9);
        n.elevation = 3;
        w.maps[0].npcs.push(n);
        let mut g = Game::new(&w, "pallet");
        let mut c = Controller::new(&g);
        advance(&mut c, &mut g, &w, [0., -1.], 24);
        assert!(c.position[1] >= 9.539);
        c.yaw = std::f32::consts::PI;
        c.tick(&mut g, &w, [0., 0.], false, true);
        assert!(g.dialogue.is_some());
        let before = c.position;
        advance(&mut c, &mut g, &w, [1., 0.], 30);
        assert_eq!(c.position, before);
    }
    #[test]
    fn elevation_and_directional_barriers_remain_enforced() {
        for behavior in [0, 0x31] {
            let mut w = open();
            let i = w.maps[0].index(Cell(11, 10));
            if behavior == 0 {
                w.maps[0].cells[i] = 0x4000;
            } else {
                w.maps[0].behaviors[i] = behavior;
            }
            let mut g = Game::new(&w, "pallet");
            let mut c = Controller::new(&g);
            advance(&mut c, &mut g, &w, [1., 0.], 60);
            assert_eq!(g.position, Cell(10, 10));
        }
    }
    #[test]
    fn ledge_jump_finishes_without_renderer() {
        let w = world();
        let m = w.map("route1");
        let start = (0..m.height() as i32 - 2)
            .flat_map(|y| (0..m.width() as i32).map(move |x| Cell(x, y)))
            .find(|p| {
                m.walkable(*p)
                    && collision(&w, "route1", *p, 3, SOUTH) == Collision::Ledge
                    && m.walkable(p.offset(Cell(0, 2)))
            })
            .unwrap();
        let mut g = Game::new(&w, "route1");
        g.position = start;
        g.elevation = m.elevation(start);
        let mut c = Controller::new(&g);
        c.warp_ready = true;
        let mut started = false;
        for _ in 0..80 {
            c.tick(&mut g, &w, [0., 1.], false, false);
            started |= g.motion.is_some();
            if started && g.motion.is_none() {
                break;
            }
        }
        assert!(started);
        assert_eq!(g.position, start.offset(Cell(0, 2)));
        assert_eq!(c.height, 0.);
    }
}
fn cell(p: [f32; 2]) -> Cell {
    Cell((p[0] + 0.5).floor() as i32, (p[1] + 0.5).floor() as i32)
}
fn distance(a: [f32; 2], b: [f32; 2]) -> f32 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}
fn facing(v: [f32; 2]) -> Cell {
    if v[0].abs() > v[1].abs() {
        Cell(v[0].signum() as i32, 0)
    } else {
        Cell(0, v[1].signum() as i32)
    }
}

/// Circle against original solid cells and static NPCs, including neighbouring maps.
pub fn clear(world: &World, map: &str, p: [f32; 2], elevation: u8) -> bool {
    let center = cell(p);
    for y in center.1 - 1..=center.1 + 1 {
        for x in center.0 - 1..=center.0 + 1 {
            let q = Cell(x, y);
            let nearest = [
                p[0].clamp(x as f32 - 0.5, x as f32 + 0.5),
                p[1].clamp(y as f32 - 0.5, y as f32 + 0.5),
            ];
            if distance(p, nearest) >= RADIUS - 0.0001 {
                continue;
            }
            let Some((id, target)) = world.destination(map, q) else {
                return false;
            };
            let m = world.map(&id);
            let e = m.elevation(target);
            if !m.terrain_walkable(target)
                || (elevation != 0 && e != 0 && e != 15 && elevation != e)
            {
                return false;
            }
            for npc in &m.npcs {
                if npc.at == target
                    && (elevation == 0 || npc.elevation == 0 || npc.elevation == elevation)
                {
                    let local = [x as f32, y as f32];
                    if distance(p, local) < RADIUS + 0.32 {
                        return false;
                    }
                }
            }
        }
    }
    true
}

#[derive(Clone, Debug)]
pub struct Controller {
    pub position: [f32; 2],
    pub yaw: f32,
    pub height: f32,
    pub moving: bool,
    warp_ready: bool,
    jump_from: [f32; 2],
}
impl Controller {
    pub fn new(game: &Game) -> Self {
        Self {
            position: [game.position.0 as f32, game.position.1 as f32],
            yaw: (game.facing.0 as f32).atan2(game.facing.1 as f32),
            height: 0.,
            moving: false,
            warp_ready: false,
            jump_from: [0., 0.],
        }
    }
    pub fn reset(&mut self, game: &Game) {
        *self = Self::new(game);
    }
    fn warp(&mut self, game: &mut Game, world: &World, at: Cell) {
        game.position = at;
        game.arrive(world);
        self.position = [game.position.0 as f32, game.position.1 as f32];
        self.warp_ready = false;
    }
    fn interact(&self, game: &mut Game, world: &World) {
        if game.dialogue.is_some() {
            game.interact(world);
            return;
        }
        let m = world.map(&game.map);
        let forward = [self.yaw.sin(), self.yaw.cos()];
        let mut candidates: Vec<Cell> = m
            .npcs
            .iter()
            .map(|n| n.at)
            .chain(m.signs.iter().map(|s| s.at))
            .collect();
        candidates.sort_by(|a, b| {
            distance(self.position, [a.0 as f32, a.1 as f32])
                .total_cmp(&distance(self.position, [b.0 as f32, b.1 as f32]))
        });
        for q in candidates {
            let target = [q.0 as f32, q.1 as f32];
            let dist = distance(self.position, target);
            if dist > 1.3 || dist < 0.01 {
                continue;
            }
            let delta = [
                (target[0] - self.position[0]) / dist,
                (target[1] - self.position[1]) / dist,
            ];
            if delta[0] * forward[0] + delta[1] * forward[1] < 0.45 {
                continue;
            }
            let mut visible = true;
            for i in 1..=20 {
                let t = i as f32 / 20.;
                let sample = cell([
                    self.position[0] + (target[0] - self.position[0]) * t,
                    self.position[1] + (target[1] - self.position[1]) * t,
                ]);
                if sample != q
                    && (!m.terrain_walkable(sample) || m.npcs.iter().any(|n| n.at == sample))
                {
                    visible = false;
                    break;
                }
            }
            if visible {
                game.interact_at(world, q);
                return;
            }
        }
    }
    /// One fixed simulation tick. Sweep short substeps and slide along blocked axes.
    pub fn tick(
        &mut self,
        game: &mut Game,
        world: &World,
        mut vector: [f32; 2],
        run: bool,
        interact: bool,
    ) {
        self.moving = false;
        if game.motion.is_some() {
            game.tick(world, Input::default());
            if let Some(m) = &game.motion {
                let t = m.progress();
                self.position = [
                    self.jump_from[0] + (m.to.0 as f32 - self.jump_from[0]) * t,
                    self.jump_from[1] + (m.to.1 as f32 - self.jump_from[1]) * t,
                ];
                self.height = (t * std::f32::consts::PI).sin() * 0.6;
            } else {
                self.position = [game.position.0 as f32, game.position.1 as f32];
                self.height = 0.;
                self.warp_ready = false;
            }
            self.moving = true;
            return;
        }
        game.ticks += 1;
        if interact {
            self.interact(game, world);
            return;
        }
        if game.dialogue.is_some() {
            return;
        }
        let length = (vector[0] * vector[0] + vector[1] * vector[1]).sqrt();
        if !length.is_finite() || length < 0.0001 {
            return;
        }
        if length > 1. {
            vector[0] /= length;
            vector[1] /= length;
        }
        let desired = vector[0].atan2(vector[1]);
        let diff = (desired - self.yaw).sin().atan2((desired - self.yaw).cos());
        self.yaw += diff.clamp(-0.22, 0.22);
        game.facing = facing(vector);
        let m = world.map(&game.map);
        let running =
            run && game.running_shoes && m.allow_running && m.behavior(game.position) != 0x0a;
        let stride = 1. / if running { 8. } else { 16. };
        let steps = (stride / 0.035_f32).ceil() as usize;
        for _ in 0..steps {
            let before = self.position;
            for axis in 0..2 {
                let amount = vector[axis] * stride / steps as f32;
                if amount.abs() < 0.00001 {
                    continue;
                }
                let mut next = self.position;
                next[axis] += amount;
                let d = if axis == 0 {
                    Cell(amount.signum() as i32, 0)
                } else {
                    Cell(0, amount.signum() as i32)
                };
                let m = world.map(&game.map);
                // Door thresholds are solid cells; trigger before the circular body hits them.
                if axis == 1 && amount < 0. && vector[1] < -0.5 && self.warp_ready {
                    if let Some(w) = m.warps.iter().find(|w| {
                        m.behavior(w.at) == 0x69
                            && (next[0] - w.at.0 as f32).abs() < 0.30
                            && self.position[1] > w.at.1 as f32 + 0.5
                            && next[1] <= w.at.1 as f32 + 0.75
                    }) {
                        self.warp(game, world, w.at);
                        return;
                    }
                }
                // Ledge jumps stay scripted, but start from an unsnapped approach point.
                if self.warp_ready
                    && facing(vector) == d
                    && collision(world, &game.map, game.position, game.elevation, d)
                        == Collision::Ledge
                {
                    let along = if axis == 0 {
                        self.position[0]
                    } else {
                        self.position[1]
                    };
                    let edge = if axis == 0 {
                        game.position.0 as f32 + d.0 as f32 * 0.5
                    } else {
                        game.position.1 as f32 + d.1 as f32 * 0.5
                    };
                    let lateral = if axis == 0 {
                        self.position[1] - game.position.1 as f32
                    } else {
                        self.position[0] - game.position.0 as f32
                    };
                    if (along - edge).abs() < RADIUS + 0.06 && lateral.abs() < 0.30 {
                        if let Some((id, to)) = world
                            .destination(&game.map, game.position.offset(Cell(d.0 * 2, d.1 * 2)))
                        {
                            if world.map(&id).walkable(to) {
                                self.jump_from = self.position;
                                game.motion = Some(Motion {
                                    from: game.position,
                                    to,
                                    target_map: id,
                                    ticks: 0,
                                    duration: 32,
                                    jump: true,
                                });
                                return;
                            }
                        }
                    }
                }
                let old_cell = cell(self.position);
                let next_cell = cell(next);
                if next_cell != old_cell
                    && collision(world, &game.map, old_cell, game.elevation, d) != Collision::None
                {
                    continue;
                }
                if !clear(world, &game.map, next, game.elevation) {
                    continue;
                }
                if let Some((id, q)) = world.destination(&game.map, next_cell) {
                    if id != game.map {
                        next[0] += (q.0 - next_cell.0) as f32;
                        next[1] += (q.1 - next_cell.1) as f32;
                        game.map = id;
                    }
                    self.position = next;
                    game.position = q;
                    game.update_elevation(world);
                }
            }
            self.moving |= distance(before, self.position) > 0.00001;
            let m = world.map(&game.map);
            if self.warp_ready {
                if let Some(w) = m.warps.iter().find(|w| {
                    m.behavior(w.at) != 0x69
                        && distance(self.position, [w.at.0 as f32, w.at.1 as f32]) < 0.30
                }) {
                    self.warp(game, world, w.at);
                    return;
                }
            } else if m
                .warps
                .iter()
                .all(|w| distance(self.position, [w.at.0 as f32, w.at.1 as f32]) > 0.8)
            {
                self.warp_ready = true;
            }
        }
    }
}
