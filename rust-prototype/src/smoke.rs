//! Actual input replay across imported rooms and Route 1, at one simulation tick per render frame.
use super::*;
use std::collections::{HashMap, VecDeque};
#[derive(Clone)]
struct Step {
    direction: Cell,
    map: String,
    to: Cell,
}
enum Goal {
    Travel(&'static str, Cell),
    Talk,
    Diagonal,
    Free,
    CheckPotion(u16),
}
#[derive(Resource)]
pub(super) struct Replay {
    pub enabled: bool,
    goals: Vec<Goal>,
    goal: usize,
    path: VecDeque<Step>,
    pending: bool,
    talk_stage: u8,
    wait: u32,
    frames: u32,
    moves: usize,
    dialogues: usize,
    free_mode: bool,
    report: PathBuf,
}
impl Replay {
    pub fn new(enabled: bool, report: PathBuf, diagonal: bool, free_mode: bool) -> Self {
        let mut replay = Self {
            enabled,
            goals: vec![
                Goal::Travel("home", Cell(4, 7)),
                Goal::Travel("home", Cell(8, 5)),
                Goal::Talk,
                Goal::Travel("bedroom", Cell(10, 3)),
                Goal::Travel("pallet", Cell(6, 8)),
                Goal::Travel("route1", Cell(6, 29)),
                Goal::Talk,
                Goal::CheckPotion(1),
                Goal::Talk,
                Goal::CheckPotion(1),
                Goal::Travel("route1", Cell(19, 17)),
                Goal::Talk,
                Goal::Travel("pallet", Cell(10, 10)),
            ],
            goal: 0,
            path: VecDeque::new(),
            pending: false,
            talk_stage: 0,
            wait: 0,
            frames: 0,
            moves: 0,
            dialogues: 0,
            free_mode,
            report,
        };
        if diagonal {
            replay
                .goals
                .splice(0..0, [Goal::Diagonal, Goal::Travel("pallet", Cell(10, 10))]);
        }
        if free_mode {
            replay
                .goals
                .splice(0..0, [Goal::Free, Goal::Travel("pallet", Cell(10, 10))]);
        }
        replay
    }
}
fn route(world: &World, start: (String, Cell), target: (String, Cell)) -> VecDeque<Step> {
    let mut todo = VecDeque::from([start.clone()]);
    let mut visited = HashMap::from([(start.clone(), None)]);
    while let Some(at) = todo.pop_front() {
        if at == target {
            break;
        }
        for d in [Cell(0, -1), Cell(0, 1), Cell(-1, 0), Cell(1, 0)] {
            let mut g = Game::new(world, &at.0);
            g.position = at.1;
            g.facing = d;
            g.elevation = world.map(&at.0).elevation(at.1);
            g.tick(
                world,
                Input {
                    direction: Some(d),
                    ..Default::default()
                },
            );
            if g.motion.is_none() {
                continue;
            }
            while g.busy() {
                g.tick(world, Input::default());
            }
            let next = (g.map.clone(), g.position);
            if !visited.contains_key(&next) {
                visited.insert(next.clone(), Some((at.clone(), d)));
                todo.push_back(next);
            }
        }
    }
    assert!(
        visited.contains_key(&target),
        "Unreachable integration target {target:?}"
    );
    let mut path = VecDeque::new();
    let mut at = target;
    while at != start {
        let (prev, d) = visited[&at].clone().unwrap();
        path.push_front(Step {
            direction: d,
            map: at.0.clone(),
            to: at.1,
        });
        at = prev;
    }
    path
}
pub(super) fn replay(
    mut r: ResMut<Replay>,
    mut s: ResMut<Simulation>,
    mut p: ResMut<Preferences>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut exit: MessageWriter<AppExit>,
) {
    if !r.enabled {
        return;
    }
    r.frames += 1;
    keys.reset_all();
    if r.frames > if r.free_mode { 8000 } else { 30_000 } {
        std::fs::write(&r.report,serde_json::json!({"passed":false,"reason":"timeout","goal":r.goal,"map":s.game.map,"position":s.free.position,"next":r.path.front().map(|v|format!("{} {:?}",v.map,v.to))}).to_string()).unwrap();
        exit.write(AppExit::error());
        r.enabled = false;
        return;
    }
    if r.free_mode && r.frames % 600 == 0 {
        std::fs::write(r.report.with_file_name("free-progress.json"),serde_json::json!({"frames":r.frames,"goal":r.goal,"map":s.game.map,"position":s.free.position,"next":r.path.front().map(|v|format!("{} {:?}",v.map,v.to))}).to_string()).unwrap();
    }
    if r.frames < 160 || s.dirty {
        return;
    }
    if r.wait > 0 {
        r.wait -= 1;
        return;
    }
    if r.goal == r.goals.len() {
        std::fs::write(&r.report,serde_json::json!({"passed":true,"moves":r.moves,"dialogues":r.dialogues,"simulation_ticks":s.game.ticks,"maps":["pallet","home","bedroom","route1"],"potions":1,"free_movement":r.free_mode,"arbitrary_heading_verified":r.free_mode}).to_string()).unwrap();
        exit.write(AppExit::Success);
        r.enabled = false;
        return;
    }
    match r.goals[r.goal] {
        Goal::Travel(map, to) => {
            if r.pending && !s.game.busy() {
                let step = r.path.front().unwrap();
                if s.game.map == step.map
                    && s.game.position == step.to
                    && (!r.free_mode
                        || ((s.free.position[0] - step.to.0 as f32).powi(2)
                            + (s.free.position[1] - step.to.1 as f32).powi(2))
                        .sqrt()
                            < 0.08)
                {
                    r.path.pop_front();
                    r.moves += 1;
                    r.pending = false;
                }
            }
            if s.game.map == map
                && s.game.position == to
                && !s.game.busy()
                && (!r.free_mode
                    || ((s.free.position[0] - to.0 as f32).powi(2)
                        + (s.free.position[1] - to.1 as f32).powi(2))
                    .sqrt()
                        < 0.08)
            {
                r.goal += 1;
                r.path.clear();
                r.pending = false;
                return;
            }
            if r.path.is_empty() {
                r.path = route(
                    &s.world,
                    (s.game.map.clone(), s.game.position),
                    (map.into(), to),
                );
                if r.path.is_empty() && r.free_mode {
                    r.path.push_back(Step {
                        map: map.into(),
                        to,
                        direction: Cell(0, 1),
                    });
                }
            }
            let step = r.path.front().unwrap();
            let direction = if r.free_mode && s.game.map == step.map {
                let delta = [
                    step.to.0 as f32 - s.free.position[0],
                    step.to.1 as f32 - s.free.position[1],
                ];
                if delta[0].abs() > delta[1].abs() {
                    Cell(if delta[0] > 0. { 1 } else { -1 }, 0)
                } else {
                    Cell(0, if delta[1] > 0. { 1 } else { -1 })
                }
            } else {
                step.direction
            };
            let key = match direction {
                Cell(0, -1) => KeyCode::KeyW,
                Cell(0, 1) => KeyCode::KeyS,
                Cell(-1, 0) => KeyCode::KeyA,
                _ => KeyCode::KeyD,
            };
            keys.press(key);
            r.pending = true;
        }
        Goal::Talk => {
            if s.game.busy() {
                return;
            }
            if r.pending && s.game.dialogue.is_none() {
                r.pending = false;
                r.talk_stage = 0;
                r.goal += 1;
                r.dialogues += 1;
                return;
            }
            if r.free_mode && r.talk_stage == 0 {
                s.free.yaw = std::f32::consts::PI;
                r.talk_stage = 1;
                return;
            }
            if r.talk_stage == 0 {
                keys.press(KeyCode::KeyW);
                r.wait = 9;
                r.talk_stage = 1;
                return;
            }
            keys.press(KeyCode::Space);
            r.pending = true;
            r.wait = 2;
        }
        Goal::Free => {
            // W at an arbitrary mouse-camera angle must retain fractional direction.
            if r.talk_stage == 0 {
                p.angle = 0.31;
                r.talk_stage = 1;
                r.wait = 0;
            }
            if r.wait < 12 {
                keys.press(KeyCode::KeyW);
                r.wait = 0;
                r.moves += 1;
            }
            if r.moves >= 12 {
                assert!(s.free.position[0].fract().abs() > 0.1);
                assert!(s.free.position[1].fract().abs() > 0.1);
                assert!(s.game.motion.is_none());
                p.angle = 0.;
                r.moves = 0;
                r.goal += 1;
                r.talk_stage = 0;
            }
        }
        Goal::Diagonal => {
            if r.pending && !s.game.busy() && s.game.position == Cell(11, 9) {
                assert_eq!(s.game.position, Cell(11, 9));
                assert_eq!(s.game.facing, Cell(1, -1));
                r.pending = false;
                r.moves += 1;
                r.goal += 1;
                return;
            }
            keys.press(KeyCode::KeyW);
            keys.press(KeyCode::KeyD);
            r.pending = true;
        }
        Goal::CheckPotion(count) => {
            assert_eq!(s.game.inventory.get("ITEM_POTION"), Some(&count));
            r.goal += 1;
        }
    }
}
