//! 3D battle stage (Settings G / `battle_3d`, default on). The FireRed battle engine is
//! unchanged; this only presents it: the Let's Go battle backdrop for the current map
//! (bin/archive/background), both Pokemon and the trainer on the field, an intro camera
//! sweep and small hit shakes. The battle UI turns into a translucent overlay.
use super::*;
use std::collections::HashMap;

/// Far from every map so the field never shows through.
pub(super) const STAGE: Vec3 = Vec3::new(4000., 0., 4000.);
/// The stage GLBs already carry the 0.01 (cm to tile) root scale.
const LG_SCALE: f32 = 1.0;
/// Player side, foe side, trainer (stage-local).
pub(super) const SPOTS: [Vec3; 3] = [Vec3::new(0., 0., 1.6), Vec3::new(0., 0., -2.2), Vec3::new(-0.3, 0., 3.9)];

pub(super) fn spot(i: usize) -> Vec3 {
    STAGE + SPOTS[i]
}
pub(super) fn facing(i: usize) -> Quat {
    // models face +Z; the player's side looks towards the foe (-Z)
    if i == 1 { Quat::IDENTITY } else { Quat::from_rotation_y(std::f32::consts::PI) }
}

#[derive(Resource, Default)]
pub(super) struct Stages {
    scenes: HashMap<String, Handle<Scene>>,
    by_map: HashMap<String, String>,
    sky: Option<Handle<Scene>>,
}
#[derive(Component)]
pub(super) struct StagePart;
#[derive(Resource, Default)]
pub(super) struct StageState {
    spawned: bool,
    started: f32,
    shake: f32,
    hp: Option<[u16; 2]>,
}

pub(super) fn setup(mut c: Commands, server: Res<AssetServer>) {
    let mut stages = Stages::default();
    let base = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.join("assets")));
    let path = base.filter(|b| b.is_dir()).unwrap_or_else(|| "assets".into()).join("letsgo/battle/battle.json");
    if let Some(manifest) = std::fs::read_to_string(path).ok().and_then(|t| serde_json::from_str::<serde_json::Value>(&t).ok()) {
        for (name, entry) in manifest.as_object().into_iter().flatten() {
            let Some(file) = entry["path"].as_str() else { continue };
            let scene: Handle<Scene> = server.load(GltfAssetLabel::Scene(0).from_asset(file.to_owned()));
            if name == "btl_sky" {
                stages.sky = Some(scene);
                continue;
            }
            for m in entry["maps"].as_array().into_iter().flatten().filter_map(|m| m.as_str()) {
                stages.by_map.insert(m.to_owned(), name.clone());
            }
            stages.scenes.insert(name.clone(), scene);
        }
    }
    c.insert_resource(stages);
    c.insert_resource(StageState::default());
}

pub(super) fn enabled(p: &Preferences, stages: &Stages) -> bool {
    p.battle_3d && !stages.scenes.is_empty()
}

pub(super) fn update(
    mut c: Commands,
    p: Res<Preferences>,
    s: Res<Simulation>,
    screen: Res<battle_screen::Screen>,
    stages: Res<Stages>,
    mut state: ResMut<StageState>,
    parts: Query<Entity, With<StagePart>>,
    time: Res<Time>,
) {
    let want = screen.active && enabled(&p, &stages);
    if want && !state.spawned {
        let name = stages.by_map.get(&s.game.map).cloned().unwrap_or_else(|| "btl_N_grass".into());
        let scene = stages.scenes.get(&name).or_else(|| stages.scenes.values().next()).cloned();
        if let Some(scene) = scene {
            c.spawn((StagePart, SceneRoot(scene), Transform::from_translation(STAGE).with_scale(Vec3::splat(LG_SCALE))));
        }
        if let Some(sky) = stages.sky.clone() {
            c.spawn((StagePart, SceneRoot(sky), Transform::from_translation(STAGE).with_scale(Vec3::splat(LG_SCALE))));
        }
        state.spawned = true;
        state.started = time.elapsed_secs();
        state.hp = None;
    } else if !want && state.spawned {
        for e in &parts {
            c.entity(e).despawn();
        }
        state.spawned = false;
    }
    if want {
        let hp = [screen.battle.mons[0].hp, screen.battle.mons[1].hp];
        if let Some(old) = state.hp {
            if hp[0] < old[0] || hp[1] < old[1] {
                state.shake = time.elapsed_secs();
            }
        }
        state.hp = Some(hp);
    }
}

/// Runs after presentation::animate and overrides the scene camera while a battle is on.
pub(super) fn camera(
    p: Res<Preferences>,
    screen: Res<battle_screen::Screen>,
    stages: Res<Stages>,
    state: Res<StageState>,
    time: Res<Time>,
    mut camera: Single<(&mut Transform, &mut Projection, Option<&mut DistanceFog>), With<SceneCamera>>,
) {
    let on = screen.active && enabled(&p, &stages) && state.spawned;
    // The backdrop reaches ~90 units and the sky ~100: push the field fog out while on stage.
    if let Some(fog) = camera.2.as_mut() {
        let falloff = if on { FogFalloff::Linear { start: 60., end: 260. } } else { FogFalloff::Linear { start: 28., end: 120. } };
        if format!("{:?}", fog.falloff) != format!("{falloff:?}") { fog.falloff = falloff; }
    }
    if !on || p.menu {
        return;
    }
    let mut t = time.elapsed_secs() - state.started;
    if std::env::args().any(|a| a == "--capture") {
        t += 3.; // QA stills show the settled view, not the intro sweep
    }
    // Main view: over the trainer's shoulder towards the foe, like Let's Go.
    let eye = STAGE + Vec3::new(0.9, 1.5, 5.6);
    let look = STAGE + Vec3::new(-0.1, 0.45, -1.0);
    // Intro: start close on the wild Pokemon, then pull back over the player's side.
    let intro_eye = spot(1) + Vec3::new(1.2, 0.7, 2.6);
    let intro_look = spot(1) + Vec3::Y * 0.35;
    let k = ((t - 0.6) / 1.6).clamp(0., 1.);
    let k = k * k * (3. - 2. * k);
    let mut eye = intro_eye.lerp(eye, k);
    let look = intro_look.lerp(look, k);
    // Idle drift and a short shake when anyone loses HP.
    eye += Vec3::new((t * 0.35).sin() * 0.25, (t * 0.5).sin() * 0.05, 0.);
    let since = time.elapsed_secs() - state.shake;
    if since < 0.35 {
        let a = (0.35 - since) * 0.18;
        eye += Vec3::new((since * 90.).sin() * a, (since * 70.).cos() * a, 0.);
    }
    *camera.0 = Transform::from_translation(eye).looking_at(look, Vec3::Y);
    *camera.1 = Projection::Perspective(PerspectiveProjection { fov: 42_f32.to_radians(), near: 0.05, far: 600., ..default() });
}

/// Battle HUD over the 3D stage: compact translucent cards at the top, the latest log
/// lines and the buttons at the bottom; restores the flat panel layout when off.
pub(super) fn hud_style(
    p: Res<Preferences>,
    stages: Res<Stages>,
    screen: Res<battle_screen::Screen>,
    mut panel: Query<(&mut Node, &mut BackgroundColor), (With<battle_screen::Panel>, Without<battle_screen::MonImage>)>,
    mut images: Query<(&mut Node, &ChildOf), (With<battle_screen::MonImage>, Without<battle_screen::Panel>)>,
    mut boxes: Query<(&mut Node, &mut BackgroundColor, Option<&ChildOf>, Has<battle_screen::Header>, Has<battle_screen::Subtitle>, Has<battle_screen::Status>), (Without<battle_screen::Panel>, Without<battle_screen::MonImage>, Without<battle_screen::Log>)>,
    mut log: Query<(&mut Node, &mut Text, &mut BackgroundColor), (With<battle_screen::Log>, Without<battle_screen::Panel>, Without<battle_screen::MonImage>)>,
    field_hud: Query<&ChildOf, With<Hud>>,
    mut c: Commands,
    mut hud_hidden: Local<bool>,
    mut last: Local<Option<bool>>,
) {
    let on = enabled(&p, &stages);
    // The field HUD box (parent of the Hud text) would sit over the stage.
    let hide = on && screen.active && !p.menu;
    if hide != *hud_hidden {
        *hud_hidden = hide;
        for parent in &field_hud {
            c.entity(parent.parent()).insert(if hide { Visibility::Hidden } else { Visibility::Inherited });
        }
    }
    if on && screen.active {
        // Only the latest lines; the full log stays in the battle state.
        for (_, mut text, _) in &mut log {
            let lines: Vec<&str> = text.0.lines().collect();
            if lines.len() > 2 {
                text.0 = lines[lines.len() - 2..].join("\n");
            }
        }
    }
    if *last == Some(on) || !screen.active {
        return;
    }
    *last = Some(on);
    let panel_color = Color::srgb_u8(19, 31, 44);
    let card_color = Color::srgb_u8(31, 48, 61);
    let mut cards = Vec::new();
    for (mut node, parent) in &mut images {
        node.display = if on { Display::None } else { Display::Flex };
        cards.push(parent.parent());
    }
    let rows: Vec<Entity> = cards.iter().filter_map(|e| boxes.get(*e).ok().and_then(|b| b.2.map(|p| p.parent()))).collect();
    for card in &cards {
        if let Ok((mut node, mut bg, ..)) = boxes.get_mut(*card) {
            node.width = if on { px(360) } else { percent(50) };
            node.padding = UiRect::all(px(if on { 12 } else { 16 }));
            bg.0 = if on { card_color.with_alpha(0.78) } else { card_color };
        }
    }
    for row in &rows {
        if let Ok((mut node, ..)) = boxes.get_mut(*row) {
            node.height = if on { Val::Auto } else { px(255) };
            node.justify_content = if on { JustifyContent::SpaceBetween } else { JustifyContent::Default };
        }
    }
    for (mut node, mut bg) in &mut panel {
        bg.0 = if on { panel_color.with_alpha(0.) } else { panel_color };
        node.padding = UiRect::all(px(if on { 20 } else { 28 }));
    }
    for (mut node, _, _, header, subtitle, status) in &mut boxes {
        if header || subtitle || status {
            node.display = if on { Display::None } else { Display::Flex };
        }
    }
    for (mut node, _, mut bg) in &mut log {
        node.flex_grow = if on { 0. } else { 1. };
        node.margin = if on { UiRect { top: Val::Auto, ..default() } } else { UiRect::ZERO };
        node.padding = if on { UiRect::all(px(12)) } else { UiRect::ZERO };
        bg.0 = if on { card_color.with_alpha(0.78) } else { Color::NONE };
    }
}
