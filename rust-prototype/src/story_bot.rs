//! QA story bot: `--bot <script.txt>` drives the real game with a tiny command script and
//! logs every map change, dialogue page, battle and script error to stderr ("[bot] ...").
//! It only presses keys (runs before `input`), so it exercises the same paths as a player.
//!
//! Commands (one per line, `#` comments):
//!   walk <up|down|left|right> <steps>   grid steps; stops early if a script takes over
//!   face <dir>                         tap a direction
//!   a                                  press interact once
//!   talk [seconds]                     keep advancing text until nothing is open
//!   yes | no                           answer a yes/no (or pick choice 1 / 2)
//!   key <KeyCode name>                 press one key (Digit1..Digit4, Escape, Space, ...)
//!   wait <frames>
//!   until <map>                        advance text until the player is on that map
//!   battle                             wait for a battle, use move 1 until it ends
//!   capture <name>                     screenshot to ../captures/bot_<name>.png (no exit)
//!   log                                print map / cell / party / flags summary
//!   end                                exit (report written to ../tmp/bot-report.json)
use super::*;
use bevy::render::view::screenshot::{save_to_disk, Screenshot};
use std::collections::VecDeque;

#[derive(Resource, Default)]
pub(super) struct Bot {
    pub enabled: bool,
    lines: VecDeque<String>,
    current: Option<(String, u32)>,
    steps_left: u32,
    last_cell: Option<(String, firered_native_prototype::Cell)>,
    stuck: u32,
    quiet: u32,
    last_text: String,
    last_map: String,
    last_error: String,
    errors: Vec<String>,
    log: Vec<String>,
    frame: u32,
}

impl Bot {
    pub fn from_args() -> Self {
        let args: Vec<String> = std::env::args().collect();
        let Some(path) = args.iter().position(|a| a == "--bot").and_then(|i| args.get(i + 1)) else { return Self::default() };
        let text = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("Cannot read bot script {path}: {e}"));
        let lines = text.lines().map(|l| l.split('#').next().unwrap_or("").trim().to_string()).filter(|l| !l.is_empty()).collect();
        Self { enabled: true, lines, ..default() }
    }
    fn note(&mut self, s: String) {
        eprintln!("[bot] {s}");
        self.log.push(s);
    }
}

fn dir_key(d: &str) -> Option<KeyCode> {
    Some(match d {
        "up" => KeyCode::ArrowUp,
        "down" => KeyCode::ArrowDown,
        "left" => KeyCode::ArrowLeft,
        "right" => KeyCode::ArrowRight,
        _ => return None,
    })
}

fn key_named(n: &str) -> Option<KeyCode> {
    Some(match n {
        "Space" => KeyCode::Space,
        "Enter" => KeyCode::Enter,
        "Escape" => KeyCode::Escape,
        "Tab" => KeyCode::Tab,
        "Digit1" => KeyCode::Digit1,
        "Digit2" => KeyCode::Digit2,
        "Digit3" => KeyCode::Digit3,
        "Digit4" => KeyCode::Digit4,
        "Digit5" => KeyCode::Digit5,
        "KeyC" => KeyCode::KeyC,
        "KeyX" => KeyCode::KeyX,
        "KeyI" => KeyCode::KeyI,
        "Backspace" => KeyCode::Backspace,
        _ => return dir_key(&n.to_lowercase()),
    })
}

#[allow(clippy::too_many_arguments)]
pub(super) fn run(
    mut bot: ResMut<Bot>,
    s: Res<Simulation>,
    field: Res<field_screen::Field>,
    battle: Res<battle_screen::Screen>,
    session: Res<persistence::Session>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut commands: Commands,
    mut exit: MessageWriter<AppExit>,
) {
    if !bot.enabled {
        return;
    }
    bot.frame += 1;
    keys.reset_all();
    // --- observation ---------------------------------------------------------------
    if s.game.map != bot.last_map {
        let m = s.game.map.clone();
        bot.note(format!("map {m} at {:?}", s.game.position));
        bot.last_map = m;
    }
    let text = s.game.dialogue.as_ref().map(|(name, pages, i)| format!("{name}: {}", pages.get(*i).cloned().unwrap_or_default())).unwrap_or_default();
    if !text.is_empty() && text != bot.last_text {
        bot.note(format!("text {}", text.replace('\n', " / ")));
    }
    bot.last_text = text;
    if !session.error.is_empty() && session.error != bot.last_error {
        let e = session.error.clone();
        bot.note(format!("ERROR {e}"));
        bot.errors.push(e.clone());
        bot.last_error = e;
    }
    if bot.frame < 120 || s.dirty {
        return;
    }
    let busy = s.game.dialogue.is_some() || field.active() || s.game.busy();
    // --- current command -----------------------------------------------------------
    if bot.current.is_none() {
        let Some(line) = bot.lines.pop_front() else {
            finish(&mut bot, &s, &session, &mut exit, true);
            return;
        };
        bot.note(format!("> {line}"));
        bot.steps_left = line.split_whitespace().nth(2).and_then(|v| v.parse().ok()).unwrap_or(0);
        bot.last_cell = Some((s.game.map.clone(), s.game.position));
        bot.stuck = 0;
        bot.quiet = 0;
        bot.current = Some((line, 0));
    }
    let (line, t) = bot.current.clone().unwrap();
    let t = t + 1;
    bot.current = Some((line.clone(), t));
    let mut parts = line.split_whitespace();
    let cmd = parts.next().unwrap_or("");
    let arg = parts.next().unwrap_or("");
    let done = |bot: &mut Bot| bot.current = None;
    match cmd {
        "walk" => {
            let cell = (s.game.map.clone(), s.game.position);
            if bot.last_cell.as_ref() != Some(&cell) {
                bot.last_cell = Some(cell);
                bot.steps_left = bot.steps_left.saturating_sub(1);
                bot.stuck = 0;
            } else {
                bot.stuck += 1;
            }
            if bot.steps_left == 0 {
                done(&mut bot);
            } else if field.active() || s.game.dialogue.is_some() {
                { let msg = format!("walk interrupted by a script/dialogue with {} steps left", bot.steps_left); bot.note(msg); }
                done(&mut bot);
            } else if bot.stuck > 150 {
                { let msg = format!("walk blocked at {:?} with {} steps left", s.game.position, bot.steps_left); bot.note(msg); }
                done(&mut bot);
            } else if let Some(k) = dir_key(arg) {
                keys.press(k);
            }
        }
        "face" => {
            if t < 4 { if let Some(k) = dir_key(arg) { keys.press(k); } } else { done(&mut bot); }
        }
        "a" => {
            if t == 1 { keys.press(KeyCode::Space); }
            if t > 10 { done(&mut bot); }
        }
        "yes" | "no" => {
            if t == 1 { keys.press(if cmd == "yes" { KeyCode::Digit1 } else { KeyCode::Digit2 }); }
            if t > 10 { done(&mut bot); }
        }
        "key" => {
            if t == 1 { if let Some(k) = key_named(arg) { keys.press(k); } else { bot.note(format!("unknown key {arg}")); } }
            if t > 10 { done(&mut bot); }
        }
        "wait" => {
            if t >= arg.parse().unwrap_or(60) { done(&mut bot); }
        }
        "talk" | "until" => {
            let limit = if cmd == "talk" { arg.parse::<u32>().unwrap_or(40) * 60 } else { 60 * 90 };
            if busy || battle.active {
                bot.quiet = 0;
                if t % 14 == 0 && !battle.active { keys.press(KeyCode::Space); }
            } else {
                bot.quiet += 1;
            }
            let reached = cmd == "until" && s.game.map == arg && !busy;
            if (cmd == "talk" && bot.quiet > 40) || reached {
                done(&mut bot);
            } else if t > limit {
                bot.note(format!("{cmd} timed out (map {}, busy {busy}, battle {})", s.game.map, battle.active));
                done(&mut bot);
            }
        }
        "battle" => {
            if battle.active {
                bot.quiet = 0;
                if t % 20 == 0 {
                    keys.press(if battle.battle.winner.is_some() { KeyCode::Escape } else { KeyCode::Digit1 });
                }
            } else if t > 60 * 120 {
                bot.note("battle timed out".into());
                done(&mut bot);
            } else if bot.quiet > 0 || t > 30 {
                // before the battle starts or after it ended: keep text moving
                if busy && t % 14 == 0 { keys.press(KeyCode::Space); }
                if bot.steps_left == 1 && !busy { bot.quiet += 1; }
                if bot.quiet > 60 { done(&mut bot); }
            }
            if battle.active && bot.steps_left == 0 {
                bot.steps_left = 1;
                bot.note(format!("battle vs {} Lv{}", battle.battle.mons[1].name, battle.battle.mons[1].level));
            }
            if !battle.active && bot.steps_left == 1 && bot.quiet == 0 {
                bot.note(format!("battle over, winner {:?}", battle.battle.winner));
                bot.quiet = 1;
            }
        }
        "capture" => {
            if t == 1 {
                let path = format!("../captures/bot_{arg}.png");
                commands.spawn(Screenshot::primary_window()).observe(save_to_disk(path));
            }
            if t > 20 { done(&mut bot); }
        }
        "log" => {
            let party: Vec<String> = session.state.party.iter().map(|m| format!("species{} Lv{} {}hp", m.species, m.level, m.hp)).collect();
            bot.note(format!("state map {} cell {:?} party {party:?} money {}", s.game.map, s.game.position, session.state.money));
            done(&mut bot);
        }
        "end" => {
            finish(&mut bot, &s, &session, &mut exit, true);
        }
        _ => {
            bot.note(format!("unknown command {line}"));
            done(&mut bot);
        }
    }
}

fn finish(bot: &mut Bot, s: &Simulation, session: &persistence::Session, exit: &mut MessageWriter<AppExit>, _ok: bool) {
    let report = serde_json::json!({
        "map": s.game.map, "cell": [s.game.position.0, s.game.position.1],
        "party": session.state.party.len(), "errors": bot.errors, "log": bot.log,
    });
    let _ = std::fs::write("../tmp/bot-report.json", report.to_string());
    { let msg = format!("finished on {} {:?}, {} errors", s.game.map, s.game.position, bot.errors.len()); bot.note(msg); }
    bot.enabled = false;
    exit.write(if bot.errors.is_empty() { AppExit::Success } else { AppExit::error() });
}
