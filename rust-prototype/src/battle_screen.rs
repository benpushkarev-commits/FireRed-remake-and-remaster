//! Native UI adapter. World wild battles update only the native session state.
use super::{Preferences, Simulation};
use bevy::prelude::*;
use firered_native_prototype::battle::{Battle, Data};

#[derive(Resource)]
pub(super) struct Screen {
    pub active: bool,
    pub integrated: bool,
    pub battle: Battle,
    pub data: Data,
    pub error: String,
}
impl Screen {
    pub fn new(active: bool) -> Self {
        let data = Data::source();
        let battle = Battle::new(&data, 0x1234);
        Self {
            active,
            integrated: false,
            battle,
            data,
            error: String::new(),
        }
    }
    fn act(&mut self, action: Action, session: &mut super::persistence::Session) {
        self.error.clear();
        match action {
            Action::Move(slot) => {
                if let Err(e) = self.battle.play(&self.data, slot) {
                    self.error = e;
                }
            }
            Action::Reset => {
                if self.integrated {
                    self.error = "A world battle cannot be restarted.".into();
                } else {
                    self.battle = Battle::new(&self.data, 0x1234);
                }
            }
            Action::Close => {
                if self.integrated && self.battle.winner.is_none() {
                    self.error = "Finish this world battle before returning.".into();
                } else {
                    self.active = false;
                }
            }
            Action::Run => {
                if !self.battle.is_wild {
                    self.error = "OAK: You cannot run from a TRAINER battle!".into();
                } else if self.battle.winner.is_some() {
                    self.active = false;
                } else {
                    let escaped = self.battle.flee();
                    if !escaped {
                        if let Err(e) = self.battle.enemy_turn(&self.data) {
                            self.error = e;
                        }
                    }
                }
            }
            Action::Catch => {
                if !self.battle.is_wild {
                    self.error = "The trainer blocked the BALL!".into();
                } else if self.battle.winner.is_some() {
                    self.error = "The battle has already ended!".into();
                } else if session.state.player_and_storage_full() {
                    self.error = "The Pokémon Boxes are full!".into();
                } else {
                    let ball = session.data.ids["ITEM_POKE_BALL"];
                    if !session.state.bag.remove(&session.data, ball, 1) {
                        self.error = "No POKE BALLS are available.".into();
                    } else {
                        match self.battle.catch(ball) {
                            Ok(false) => {
                                if let Err(e) = self.battle.enemy_turn(&self.data) {
                                    self.error = e;
                                }
                            }
                            Ok(true) => {}
                            Err(e) => {
                                // Unsupported ball logic must not consume the item.
                                let super::persistence::Session { data, state, .. } = &mut *session;
                                let firered_native_prototype::core::State { bag, events, .. } =
                                    state;
                                let _ = bag.add(data, events, ball, 1);
                                self.error = e;
                            }
                        }
                    }
                }
            }
        }
    }
}

fn sprite_path(name: &str, is_foe: bool) -> String {
    let lower = name.to_lowercase();
    if is_foe {
        if lower == "squirtle" {
            "battle/squirtle.png".into()
        } else {
            format!("battle/{lower}_front.png")
        }
    } else {
        if lower == "squirtle" {
            "battle/squirtle_back.png".into()
        } else {
            format!("battle/{lower}.png")
        }
    }
}
#[derive(Component)]
pub(super) struct Header;
#[derive(Component)]
pub(super) struct Subtitle;
#[derive(Component)]
pub(super) struct Panel;
#[derive(Component)]
pub(super) struct Status;
#[derive(Component)]
pub(super) struct Log;
#[derive(Component)]
pub(super) struct MonLabel(usize);
#[derive(Component)]
pub(super) struct HpBar(usize);
#[derive(Component)]
pub(super) struct MonImage(usize);
#[derive(Component)]
pub(super) struct ButtonLabel(Action);
#[derive(Component, Clone, Copy, PartialEq, Eq)]
pub(super) enum Action {
    Move(usize),
    Catch,
    Run,
    Reset,
    Close,
}

pub(super) fn setup(mut c: Commands, server: Res<AssetServer>) {
    c.spawn((Panel,GlobalZIndex(200),Node {
        position_type:PositionType::Absolute,width:percent(100),height:percent(100),
        flex_direction:FlexDirection::Column,padding:UiRect::all(px(28)),row_gap:px(12),
        display:Display::None,..default()
    },BackgroundColor(Color::srgb_u8(19,31,44))))
    .with_children(|panel| {
        panel.spawn((Header,Text::new("FIRST RIVAL BATTLE  |  NATIVE RUST"),TextFont{font_size:28.,..default()},TextColor(Color::WHITE)));
        panel.spawn((Subtitle,Text::new("Original FireRed trainer AI and four opening moves. Fixed test starter and random seed."),TextFont{font_size:17.,..default()},TextColor(Color::srgb_u8(168,191,209))));
        panel.spawn(Node{width:percent(100),height:px(255),column_gap:px(24),..default()}).with_children(|row| {
            for (index,name) in [(0,"charmander"),(1,"squirtle")] {
                row.spawn((Node{width:percent(50),padding:UiRect::all(px(16)),column_gap:px(16),align_items:AlignItems::Center,..default()},BackgroundColor(Color::srgb_u8(31,48,61)))).with_children(|card| {
                    card.spawn((MonImage(index),ImageNode::new(server.load(format!("battle/{name}.png"))),Node{width:px(192),height:px(192),..default()}));
                    card.spawn(Node{flex_direction:FlexDirection::Column,flex_grow:1.,row_gap:px(12),..default()}).with_children(|info| {
                        info.spawn((MonLabel(index),Text::new(""),TextFont{font_size:22.,..default()},TextColor(Color::WHITE)));
                        info.spawn((Node{width:percent(100),height:px(12),..default()},BackgroundColor(Color::srgb_u8(10,20,25)))).with_children(|track| {
                            track.spawn((HpBar(index),Node{width:percent(100),height:percent(100),..default()},BackgroundColor(Color::srgb_u8(101,205,131))));
                        });
                    });
                });
            }
        });
        panel.spawn((Status,Text::new(""),TextFont{font_size:17.,..default()},TextColor(Color::srgb_u8(168,191,209))));
        panel.spawn((Log,Text::new(""),Node{flex_grow:1.,..default()},BackgroundColor(Color::NONE),TextFont{font_size:21.,..default()},TextColor(Color::srgb_u8(255,244,211))));
        panel.spawn(Node{column_gap:px(10),height:px(54),flex_wrap:FlexWrap::Wrap,row_gap:px(8),..default()}).with_children(|row| {
            for (action,label) in [
                (Action::Move(0),"1 Move 1"),
                (Action::Move(1),"2 Move 2"),
                (Action::Move(2),"3 Move 3"),
                (Action::Move(3),"4 Move 4"),
                (Action::Catch,"C Poké Ball"),
                (Action::Run,"5 Run"),
                (Action::Reset,"R Restart"),
                (Action::Close,"Esc Return"),
            ] {
                row.spawn((Button,action,Node{padding:UiRect::all(px(12)),align_items:AlignItems::Center,justify_content:JustifyContent::Center,..default()},BackgroundColor(Color::srgb_u8(48,76,91))))
                    .with_children(|button| {button.spawn((ButtonLabel(action),Text::new(label),TextFont{font_size:18.,..default()},TextColor(Color::WHITE)));});
            }
        });
    });
}

pub(super) fn controls(
    keys: Res<ButtonInput<KeyCode>>,
    mut screen: ResMut<Screen>,
    p: Res<Preferences>,
    mut session: ResMut<super::persistence::Session>,
    mut interactions: Query<
        (&Interaction, &Action, &mut BackgroundColor),
        (Changed<Interaction>, With<Button>),
    >,
) {
    if !screen.active || p.menu {
        return;
    }
    let action = if keys.just_pressed(KeyCode::Digit1) {
        Some(Action::Move(0))
    } else if keys.just_pressed(KeyCode::Digit2) {
        Some(Action::Move(1))
    } else if keys.just_pressed(KeyCode::Digit3) {
        if screen
            .battle
            .mons
            .first()
            .map_or(false, |m| m.moves.len() > 2)
        {
            Some(Action::Move(2))
        } else {
            Some(Action::Run)
        }
    } else if keys.just_pressed(KeyCode::Digit4) {
        Some(Action::Move(3))
    } else if keys.just_pressed(KeyCode::Digit5) {
        Some(Action::Run)
    } else if keys.just_pressed(KeyCode::KeyC) {
        Some(Action::Catch)
    } else if keys.just_pressed(KeyCode::KeyR) {
        Some(Action::Reset)
    } else if keys.just_pressed(KeyCode::Escape) {
        Some(Action::Close)
    } else {
        None
    };
    if let Some(a) = action {
        screen.act(a, &mut session);
    }
    for (interaction, action, mut color) in &mut interactions {
        color.0 = match interaction {
            Interaction::Hovered => Color::srgb_u8(67, 104, 119),
            _ => Color::srgb_u8(48, 76, 91),
        };
        if *interaction == Interaction::Pressed {
            screen.act(*action, &mut session);
        }
    }
}
pub(super) fn draw(
    screen: Res<Screen>,
    p: Res<Preferences>,
    mut panels: Query<&mut Node, With<Panel>>,
    mut labels: Query<(&MonLabel, &mut Text)>,
    mut bars: Query<(&HpBar, &mut Node), Without<Panel>>,
    mut log: Query<
        &mut Text,
        (
            With<Log>,
            Without<MonLabel>,
            Without<Status>,
            Without<ButtonLabel>,
        ),
    >,
    mut status: Query<
        &mut Text,
        (
            With<Status>,
            Without<MonLabel>,
            Without<Log>,
            Without<ButtonLabel>,
        ),
    >,
    server: Res<AssetServer>,
    mut images: Query<(&MonImage, &mut ImageNode)>,
    previews: Res<super::models::Previews>,
    models: Query<&super::models::Character>,
    mut buttons: Query<
        (&ButtonLabel, &mut Text),
        (
            Without<MonLabel>,
            Without<Log>,
            Without<Status>,
            Without<Header>,
            Without<Subtitle>,
        ),
    >,
    mut headers: Query<
        &mut Text,
        (
            With<Header>,
            Without<Subtitle>,
            Without<MonLabel>,
            Without<Log>,
            Without<Status>,
            Without<ButtonLabel>,
        ),
    >,
    mut subtitles: Query<
        &mut Text,
        (
            With<Subtitle>,
            Without<Header>,
            Without<MonLabel>,
            Without<Log>,
            Without<Status>,
            Without<ButtonLabel>,
        ),
    >,
) {
    // Scene loading completes independently of turn changes. Update portraits
    // when ready while retaining stable text shaping between actions.
    if screen.active {
        for (img, mut node) in &mut images {
            let name = &screen.battle.mons[img.0].name;
            let image = if p.pokemon_models && super::models::preview_ready(&models, img.0, name) {
                previews.images[img.0].clone()
            } else { server.load(sprite_path(name, img.0 == 1)) };
            if node.image != image { node.image = image; }
        }
    }
    // Keep text shaping stable between actions; this screen is turn-driven.
    if !screen.is_changed() && !p.is_changed() {
        return;
    }
    for mut panel in &mut panels {
        panel.display = if screen.active && !p.menu {
            Display::Flex
        } else {
            Display::None
        };
    }
    if !screen.active {
        return;
    }
    for mut text in &mut headers {
        text.0 = if screen.battle.is_wild {
            "WILD POKÉMON BATTLE  |  NATIVE RUST".into()
        } else {
            "FIRST RIVAL BATTLE  |  NATIVE RUST".into()
        };
    }
    for mut text in &mut subtitles {
        text.0 = if screen.battle.is_wild {
            "Wild battle. Throw Poké Balls with [C], fight with [1-4], or flee with [5].".into()
        } else {
            "Original FireRed trainer AI and four opening moves. Fixed test starter and random seed.".into()
        };
    }
    for (label, mut text) in &mut labels {
        let m = &screen.battle.mons[label.0];
        text.0 = format!(
            "{}  Lv. {}\nHP  {} / {}\nAtk {}   Def {}\nStages {:+} / {:+}",
            m.name,
            m.level,
            m.hp,
            m.stats[0],
            m.stats[1],
            m.stats[2],
            m.stages[0] as i8 - 6,
            m.stages[1] as i8 - 6
        );
    }
    for (bar, mut node) in &mut bars {
        let m = &screen.battle.mons[bar.0];
        node.width = percent(100. * m.hp as f32 / m.stats[0] as f32);
    }
    for mut text in &mut log {
        text.0 = screen.battle.log.join("\n");
        if !screen.error.is_empty() {
            text.0.push_str(&format!("\n{}", screen.error));
        }
    }
    for mut text in &mut status {
        text.0 = if screen.battle.winner.is_some() {
            if screen.battle.caught {
                "Pokémon caught! Press Esc to return to field".into()
            } else if screen.battle.fled {
                "Got away safely! Press Esc to return to field".into()
            } else if screen.battle.winner == Some(0) {
                "Battle ended - You won! Press Esc or R to return".into()
            } else {
                "Battle ended - Defeated! Press Esc or R to return".into()
            }
        } else if let Some(_ai) = &screen.battle.last_ai {
            format!(
                "Turn {}   |   Enemy move used   |   Choose your move [1-4], Catch [C], or Run [5]",
                screen.battle.turn,
            )
        } else if screen.battle.is_wild {
            format!(
                "Wild {} appeared! Lv. {}. Catch with [C] or attack with [1-4].",
                screen.battle.mons[1].name, screen.battle.mons[1].level
            )
        } else {
            "Charmander: Hardy, IVs 31 / EVs 0. Rival Squirtle: original trainer-generated stats."
                .into()
        };
    }
    for (label, mut text) in &mut buttons {
        match label.0 {
            Action::Move(slot) => {
                let m = &screen.battle.mons[0];
                if slot < m.moves.len() {
                    let move_name = m.moves[slot].trim_start_matches("MOVE_").replace('_', " ");
                    text.0 = format!("{}  {}  PP {}", slot + 1, move_name, m.pp[slot]);
                } else {
                    text.0 = "-".into();
                }
            }
            Action::Catch => {
                text.0 = if screen.battle.is_wild {
                    "C  Poké Ball".into()
                } else {
                    "-".into()
                };
            }
            Action::Run => {
                text.0 = if screen.battle.is_wild {
                    "5  Run".into()
                } else {
                    "-".into()
                };
            }
            Action::Reset => {
                text.0 = if screen.integrated {
                    "-".into()
                } else {
                    "R  Restart".into()
                };
            }
            _ => {}
        }
    }
}

#[derive(Resource)]
pub(super) struct Replay {
    pub enabled: bool,
    frame: u32,
    phase: u8,
    closed: bool,
    snapshot: Option<(String, firered_native_prototype::Cell)>,
    path: std::path::PathBuf,
}
impl Replay {
    pub fn new(enabled: bool, path: std::path::PathBuf) -> Self {
        Self {
            enabled,
            frame: 0,
            phase: 0,
            closed: false,
            snapshot: None,
            path,
        }
    }
}
pub(super) fn replay(
    mut replay: ResMut<Replay>,
    screen: Res<Screen>,
    s: Res<Simulation>,
    mut keys: ResMut<ButtonInput<KeyCode>>,
    mut exit: MessageWriter<AppExit>,
) {
    if !replay.enabled {
        return;
    }
    replay.frame += 1;
    keys.reset(KeyCode::KeyB);
    keys.reset(KeyCode::Digit1);
    keys.reset(KeyCode::Digit2);
    keys.reset(KeyCode::Escape);
    keys.reset(KeyCode::KeyR);
    if replay.frame == 20 {
        replay.snapshot = Some((s.game.map.clone(), s.game.position));
        keys.press(KeyCode::KeyB);
    }
    if replay.frame == 35 {
        keys.press(KeyCode::Digit2);
    }
    if replay.frame > 45
        && replay.frame % 12 == 0
        && replay.phase == 0
        && screen.battle.winner.is_none()
    {
        keys.press(KeyCode::Digit1);
    }
    if screen.battle.winner.is_some() && replay.phase == 0 {
        replay.phase = 1;
        keys.press(KeyCode::Escape);
        std::fs::write(
            replay.path.with_file_name("battle-trace.json"),
            serde_json::to_string_pretty(&screen.battle).unwrap(),
        )
        .unwrap();
    } else if replay.phase == 1 && !screen.active {
        replay.closed = true;
        replay.phase = 2;
        keys.press(KeyCode::KeyB);
    } else if replay.phase == 2 && screen.active {
        let passed = replay.closed
            && replay.snapshot.as_ref() == Some(&(s.game.map.clone(), s.game.position))
            && screen.battle.turn == 0
            && screen.battle.mons[0].hp == 20;
        std::fs::write(&replay.path,serde_json::json!({"passed":passed,"native_input":true,"returned_to_same_world_position":passed,"restart_verified":passed}).to_string()).unwrap();
        exit.write(if passed {
            AppExit::Success
        } else {
            AppExit::error()
        });
        replay.enabled = false;
    }
    if replay.frame > 1500 {
        exit.write(AppExit::error());
        replay.enabled = false;
    }
}
