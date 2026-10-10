//! Imported scene hierarchies keep skin joints, materials and authored clips intact.
use super::*;
use bevy::camera::{RenderTarget, visibility::RenderLayers};
use bevy::core_pipeline::tonemapping::Tonemapping;
use bevy::render::render_resource::TextureFormat;
use bevy::scene::SceneInstanceReady;
use std::collections::HashMap;
use std::time::Duration;

#[derive(Clone, Copy)]
pub(super) enum Role {
    Player,
    Npc(u16),
    Battle(usize),
    /// The player's trainer standing behind their Pokemon on the 3D battle stage.
    Trainer,
}
#[derive(Component)]
pub(super) struct Character {
    pub key: String,
    pub ready: bool,
    role: Role,
    previous: Option<Vec3>,
    speed: f32,
    motion: usize,
    /// Battle reactions: HP seen last frame for (self, opponent) and the one-shot clip playing.
    last_hp: Option<(u16, u16)>,
    action: Option<(usize, f32)>,
}
struct Model {
    gltf: Handle<bevy::gltf::Gltf>,
    scene: Handle<Scene>,
    /// Tiles per second the Idle/Walk/Run clip covers at 1x (0 = unknown); used to
    /// pick the clip and playback rate so feet match ground speed.
    speeds: [f32; 3],
}
#[derive(Resource)]
pub(super) struct Catalog(HashMap<String, Model>);
/// Let's Go field scenery per slice map id, imported locally by tools/letsgo/import_letsgo_field.py.
/// Absent manifest = no scenery; the FireRed block scenery is used instead.
#[derive(Resource, Default)]
pub(super) struct Scenery {
    pub scenes: HashMap<String, Vec<Handle<Scene>>>,
    /// Maps whose FireRed layout differs: kit pieces dress FireRed features on Let's Go terrain.
    pub overlay: std::collections::HashSet<String>,
    pub heights: HashMap<String, Heights>,
}
/// Let's Go ground height (tiles) sampled on a FireRed tile grid, plus kept Let's Go tree trunks.
pub(super) struct Heights {
    res: f32,
    width: usize,
    height: usize,
    grid: Vec<f32>,
    pub trees: Vec<Vec2>,
}
impl Heights {
    pub fn at(&self, x: f32, z: f32) -> f32 {
        let (w, h) = (self.width as f32 * self.res, self.height as f32 * self.res);
        let gx = ((x + 0.5) * self.res - 0.5).clamp(0., w - 1.);
        let gz = ((z + 0.5) * self.res - 0.5).clamp(0., h - 1.);
        let (x0, z0) = (gx.floor() as usize, gz.floor() as usize);
        let (x1, z1) = ((x0 + 1).min(w as usize - 1), (z0 + 1).min(h as usize - 1));
        let (fx, fz) = (gx - x0 as f32, gz - z0 as f32);
        let row = w as usize;
        let g = |i: usize, j: usize| self.grid[j * row + i];
        let top = g(x0, z0) * (1. - fx) + g(x1, z0) * fx;
        let bottom = g(x0, z1) * (1. - fx) + g(x1, z1) * fx;
        top * (1. - fz) + bottom * fz
    }
}
impl Scenery {
    /// Ground height under a map-local position (0 when the map has no Let's Go terrain or it is off).
    pub fn ground(&self, enabled: bool, map: &str, x: f32, z: f32) -> f32 {
        if !enabled { return 0. }
        self.heights.get(map).map_or(0., |h| h.at(x, z))
    }
}
/// Let's Go kit pieces (tree, grass, flowers, ground textures) present in assets/letsgo/kit.
#[derive(Resource, Default)]
pub(super) struct Kit {
    pub ready: bool,
}
fn load_scenery(server: &AssetServer) -> Scenery {
    let base = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.join("assets")));
    let path = base.filter(|b| b.is_dir()).unwrap_or_else(|| "assets".into()).join("letsgo/scenery.json");
    let Ok(text) = std::fs::read_to_string(&path) else { return Scenery::default() };
    let manifest: serde_json::Value = match serde_json::from_str(&text) {
        Ok(v) => v,
        Err(e) => { eprintln!("Let's Go scenery manifest error: {e}"); return Scenery::default() }
    };
    let root = path.parent().and_then(|p| p.parent()).map(|p| p.to_path_buf()).unwrap_or_default();
    let mut out = Scenery::default();
    for (map, entry) in manifest.as_object().into_iter().flatten() {
        let scenes = entry["scenes"].as_array().into_iter().flatten()
            .filter_map(|e| e["path"].as_str())
            .map(|p| server.load(GltfAssetLabel::Scene(0).from_asset(p.to_owned())))
            .collect();
        out.scenes.insert(map.clone(), scenes);
        if entry["overlay"].as_bool() == Some(true) {
            out.overlay.insert(map.clone());
        }
        let heights = entry["heights"].as_str().and_then(|p| std::fs::read_to_string(root.join(p)).ok())
            .and_then(|t| serde_json::from_str::<serde_json::Value>(&t).ok());
        if let Some(h) = heights {
            let num = |k: &str| h[k].as_u64().unwrap_or(0) as usize;
            let grid: Vec<f32> = h["grid"].as_array().into_iter().flatten()
                .flat_map(|row| row.as_array().cloned().unwrap_or_default())
                .map(|v| v.as_f64().unwrap_or(0.) as f32).collect();
            let trees = h["trees"].as_array().into_iter().flatten()
                .filter_map(|t| Some(Vec2::new(t[0].as_f64()? as f32, t[1].as_f64()? as f32))).collect();
            let res = num("res").max(1);
            if grid.len() == num("width") * num("height") * res * res && !grid.is_empty() {
                out.heights.insert(map.clone(), Heights { res: res as f32, width: num("width"), height: num("height"), grid, trees });
            }
        }
    }
    out
}
#[derive(Resource)]
pub(super) struct Previews {
    pub images: [Handle<Image>; 2],
}
#[derive(Component)]
pub(super) struct Playback {
    owner: Entity,
    /// Idle, Walk, Run, then optional Attack, Hurt, Faint (missing ones fall back to Idle).
    clips: [AnimationNodeIndex; 6],
    current: usize,
}
const MOTIONS: [&str; 6] = ["Idle", "Walk", "Run", "Attack", "Hurt", "Faint"];
#[derive(Resource)]
pub(super) struct AnimationPreview(Option<usize>);
#[derive(Component)]
pub(super) struct PreviewCamera;

pub(super) fn npc_key(graphics: &str) -> Option<&'static str> {
    Some(match graphics {
        "OBJ_EVENT_GFX_RIVAL" | "OBJ_EVENT_GFX_BLUE" => "blue",
        "OBJ_EVENT_GFX_BROCK" => "brock",
        "OBJ_EVENT_GFX_YOUNGSTER" | "OBJ_EVENT_GFX_BOY" => "youngster",
        "OBJ_EVENT_GFX_CAMPER" => "camper",
        "OBJ_EVENT_GFX_WOMAN_1" | "OBJ_EVENT_GFX_WOMAN_2" | "OBJ_EVENT_GFX_WOMAN_3" => "woman",
        "OBJ_EVENT_GFX_FAT_MAN" => "fat_man",
        "OBJ_EVENT_GFX_SCIENTIST" => "scientist",
        "OBJ_EVENT_GFX_CLERK" => "clerk",
        "OBJ_EVENT_GFX_GIRL" | "OBJ_EVENT_GFX_LASS" => "girl",
        "OBJ_EVENT_GFX_OLD_MAN_1" | "OBJ_EVENT_GFX_OLD_MAN_2" => "old_man",
        "OBJ_EVENT_GFX_PIDGEY" => "pidgey",
        "OBJ_EVENT_GFX_RATTATA" => "rattata",
        "OBJ_EVENT_GFX_BULBASAUR" => "bulbasaur",
        "OBJ_EVENT_GFX_CHARMANDER" => "charmander",
        "OBJ_EVENT_GFX_SQUIRTLE" => "squirtle",
        _ => return None,
    })
}
pub(super) fn spawn(c: &mut Commands, catalog: &Catalog, key: &str, role: Role, at: Vec3) -> Entity {
    c.spawn((
        Character { key: key.into(), ready: false, role, previous: None, speed: 0., motion: 0, last_hp: None, action: None },
        SceneRoot(catalog.0[key].scene.clone()),
        Transform::from_translation(at),
        Visibility::Hidden,
    ))
    .observe(scene_ready)
    .id()
}
pub(super) fn setup(
    mut c: Commands,
    server: Res<AssetServer>,
    mut images: ResMut<Assets<Image>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    let manifest: Vec<serde_json::Value> = serde_json::from_str(include_str!("../assets/models/slice-models.json")).expect("Slice model manifest");
    let mut catalog = Catalog(HashMap::new());
    for entry in manifest {
        let key = entry["key"].as_str().unwrap();
        let path = entry["file"].as_str().unwrap();
        let mut speeds = [0.; 3];
        for (i, v) in entry["clip_speed"].as_array().into_iter().flatten().take(3).enumerate() {
            speeds[i] = v.as_f64().unwrap_or(0.) as f32;
        }
        catalog.0.insert(key.into(), Model {
            gltf: server.load(path.to_owned()),
            scene: server.load(GltfAssetLabel::Scene(0).from_asset(path.to_owned())),
            speeds,
        });
    }
    let args: Vec<String> = std::env::args().collect();
    let key = args.iter().position(|a| a == "--model-key").and_then(|i| args.get(i + 1)).map_or("red", String::as_str);
    assert!(catalog.0.contains_key(key), "Unknown preview model {key}");
    spawn(&mut c, &catalog, key, Role::Player, Vec3::ZERO);
    c.insert_resource(AnimationPreview(if args.iter().any(|a| a == "--model-walk") { Some(1) }
        else if args.iter().any(|a| a == "--model-run") { Some(2) } else { None }));
    let previews = Previews { images: std::array::from_fn(|_| images.add(Image::new_target_texture(512, 512, TextureFormat::Rgba8UnormSrgb))) };
    for index in 0..2 {
        let origin = preview_origin(index);
        let layers = RenderLayers::layer(index + 1);
        c.spawn((
            PreviewCamera, Camera3d::default(),
            Camera { target: RenderTarget::Image(previews.images[index].clone().into()),
                order: -2, is_active: false, clear_color: ClearColorConfig::Custom(Color::srgb_u8(31, 48, 61)), ..default() },
            Transform::from_translation(origin + Vec3::new(1.5, 1.1, 2.6)).looking_at(origin + Vec3::Y * 0.45, Vec3::Y),
            Projection::Perspective(PerspectiveProjection { fov: 35_f32.to_radians(), near: 0.05, ..default() }),
            Tonemapping::None, Msaa::Off, layers.clone(),
        ));
        c.spawn((
            DirectionalLight { illuminance: 2800., shadows_enabled: false, ..default() },
            Transform::from_xyz(-3., 6., 4.).looking_at(Vec3::ZERO, Vec3::Y), layers.clone(),
        ));
        c.spawn((Mesh3d(meshes.add(Cylinder::new(0.75, 0.035))),
            MeshMaterial3d(materials.add(StandardMaterial { base_color: Color::srgb_u8(78, 102, 81), perceptual_roughness: 1., ..default() })),
            Transform::from_translation(origin - Vec3::Y * 0.02), layers,
        ));
    }
    c.insert_resource(previews);
    c.insert_resource(catalog);
    c.insert_resource(load_scenery(&server));
    let base = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.join("assets")));
    let kit = base.filter(|b| b.is_dir()).unwrap_or_else(|| "assets".into()).join("letsgo/kit/kit.json");
    c.insert_resource(Kit { ready: kit.exists() });
}
fn preview_origin(index: usize) -> Vec3 {
    Vec3::new(2000. + index as f32 * 10., 0., 2000.)
}
fn scene_ready(
    event: On<SceneInstanceReady>,
    mut c: Commands,
    mut roots: Query<&mut Character>,
    children: Query<&Children>,
    catalog: Res<Catalog>,
    gltfs: Res<Assets<bevy::gltf::Gltf>>,
    mut graphs: ResMut<Assets<AnimationGraph>>,
    mut players: Query<&mut AnimationPlayer>,
) {
    let Ok(mut root) = roots.get_mut(event.entity) else { return };
    root.ready = true;
    eprintln!("[slice-model] ready {}", root.key);
    // Battle models are on the main layer too, for the 3D battle stage.
    let layers = match root.role { Role::Battle(i) => RenderLayers::from_layers(&[0, i + 1]), _ => RenderLayers::layer(0) };
    let gltf = gltfs.get(&catalog.0[&root.key].gltf);
    for child in children.iter_descendants(event.entity) {
        c.entity(child).insert(layers.clone());
        if let (Ok(mut player), Some(gltf)) = (players.get_mut(child), gltf) {
            let Some(base) = ["Idle", "Walk", "Run"].into_iter().map(|name| gltf.named_animations.get(name).cloned()).collect::<Option<Vec<_>>>() else { continue };
            let extra: Vec<_> = ["Attack", "Hurt", "Faint"].into_iter()
                .map(|name| gltf.named_animations.get(name).cloned().unwrap_or_else(|| base[0].clone())).collect();
            let (graph, indices) = AnimationGraph::from_clips(base.into_iter().chain(extra));
            let mut transitions = AnimationTransitions::new();
            transitions.play(&mut player, indices[0], Duration::ZERO).repeat();
            c.entity(child).insert((AnimationGraphHandle(graphs.add(graph)), transitions,
                Playback { owner: event.entity, clips: std::array::from_fn(|i| indices[i]), current: 0 }));
        }
    }
}
pub(super) fn update(
    mut c: Commands,
    s: Res<Simulation>, p: Res<Preferences>, time: Res<Time>,
    field: Res<field_screen::Field>, battle: Res<battle_screen::Screen>, catalog: Res<Catalog>,
    mut roots: Query<(Entity, &mut Character, &mut Transform, &mut Visibility)>,
    mut playback: Query<(&mut AnimationPlayer, &mut AnimationTransitions, &mut Playback)>,
    preview: Res<AnimationPreview>,
    mut cameras: Query<&mut Camera, With<PreviewCamera>>,
    shift: Res<presentation::WorldShift>,
    scenery: Res<Scenery>,
    stages: Res<battle_stage::Stages>,
) {
    let stage = battle_stage::enabled(&p, &stages);
    for mut camera in &mut cameras {
        camera.is_active = battle.active && p.pokemon_models && !p.menu && !stage;
    }
    let mut trainer = false;
    let mut previews = [false; 2];
    for (entity, mut root, mut transform, mut visibility) in &mut roots {
        if shift.is_changed() && matches!(root.role, Role::Player) {
            root.previous = root.previous.map(|v| v + shift.0);
            transform.translation += shift.0;
        }
        let enabled = match root.role {
            Role::Player => {
                let yaw = if s.free_active && !s.game.script_locked { s.free.yaw }
                    else { (s.game.facing.0 as f32).atan2(s.game.facing.1 as f32) };
                transform.translation = s.displayed + Vec3::Y * 0.025;
                // Turn towards the heading over a few frames instead of snapping.
                let goal = Quat::from_rotation_y(yaw);
                transform.rotation = if root.previous.is_none() { goal }
                    else { transform.rotation.slerp(goal, 1. - (-16. * time.delta_secs()).exp()) };
                p.red_model
            }
            Role::Npc(id) => {
                if p.actor_scenes && id != 0 {
                    if let Some((at, facing)) = field.actors.displayed(id) {
                        transform.translation = Vec3::new(at[0], 0.025 + scenery.ground(p.letsgo_scenery, &s.game.map, at[0], at[1]), at[1]);
                        transform.rotation = Quat::from_rotation_y((facing.0 as f32).atan2(facing.1 as f32));
                    } else {
                        *visibility = Visibility::Hidden;
                        root.previous = None;
                        root.motion = 0;
                        continue;
                    }
                }
                p.trainer_models
            }
            Role::Battle(i) => {
                let key = battle.battle.mons[i].name.to_lowercase();
                if root.key != key {
                    c.entity(entity).despawn();
                    continue;
                }
                previews[i] = true;
                let hp = (battle.battle.mons[i].hp, battle.battle.mons[1 - i].hp);
                if let Some((own, other)) = root.last_hp {
                    let now = time.elapsed_secs();
                    if hp.0 == 0 && own > 0 {
                        root.action = Some((5, f32::MAX));           // faint and stay down
                    } else if hp.0 < own {
                        root.action = Some((4, now + 0.6));          // took a hit
                    } else if hp.1 < other {
                        root.action = Some((3, now + 1.0));          // our move landed
                    }
                }
                if hp.0 > 0 && root.action.is_some_and(|(m, _)| m == 5) {
                    root.action = None;                              // revived / new battle
                }
                root.last_hp = Some(hp);
                if stage {
                    // On the stage: real clips only, facing the other side.
                    transform.translation = battle_stage::spot(i);
                    transform.rotation = battle_stage::facing(i);
                    transform.scale = Vec3::ONE;
                } else {
                    // Flat panel previews: a small planted idle sway.
                    let phase = if p.character_animation && !p.menu { time.elapsed_secs() } else { 0. };
                    transform.translation = preview_origin(i);
                    transform.rotation = Quat::from_rotation_y(-0.25 + (phase * 1.8).sin() * 0.025);
                    transform.scale = Vec3::new(1., 1. + (phase * 2.).sin() * 0.006, 1.);
                }
                battle.active && p.pokemon_models
            }
            Role::Trainer => {
                if !(battle.active && stage && p.red_model) {
                    c.entity(entity).despawn();
                    continue;
                }
                trainer = true;
                transform.translation = battle_stage::spot(2);
                transform.rotation = battle_stage::facing(2);
                true
            }
        };
        *visibility = if enabled && root.ready { Visibility::Inherited } else { Visibility::Hidden };
        let distance = root.previous.map_or(0., |at| {
            let delta = transform.translation - at;
            Vec2::new(delta.x, delta.z).length()
        });
        root.previous = Some(transform.translation);
        let sample = if distance > 0.5 { 0. } else { distance / time.delta_secs().max(0.001) };
        root.speed += (sample - root.speed) * (1. - (-time.delta_secs() * 15.).exp());
        let moving = match root.role {
            Role::Player => {
                let moving = if s.free_active && !s.game.script_locked { s.free.moving }
                    else { s.game.motion.as_ref().is_some_and(|m| m.from != m.to) || (s.game.script_locked && distance > 0.00001 && distance <= 0.5) };
                moving && !p.menu && !battle.active
            }
            Role::Npc(_) => distance > 0.00001 && !p.menu && !battle.active,
            Role::Battle(_) | Role::Trainer => false,
        };
        let grid_run = matches!(root.role, Role::Player) && !s.free_active
            && s.game.motion.as_ref().is_some_and(|m| m.duration <= 11 && !m.jump);
        let native = catalog.0.get(&root.key).map_or([0.; 3], |m| m.speeds);
        // Source clips: switch to Run once Walk would need more than its midpoint speed.
        let run_from = if native[1] > 0. && native[2] > 0. { (native[1] * native[2]).sqrt() } else { 5.3 };
        root.motion = if !enabled || !p.character_animation || !moving { 0 }
            else if root.speed > run_from || grid_run { 2 } else { 1 };
        if let Some((clip, until)) = root.action {
            if time.elapsed_secs() < until && p.character_animation { root.motion = clip; } else { root.action = None; }
        }
        if matches!(root.role, Role::Player) && p.character_animation && !p.menu {
            if let Some(motion) = preview.0 { root.motion = motion; }
        }
    }
    let want_trainer = battle.active && stage && p.red_model;
    if want_trainer && !trainer && catalog.0.contains_key("red") {
        spawn(&mut c, &catalog, "red", Role::Trainer, battle_stage::spot(2));
    }
    if battle.active && p.pokemon_models {
        for (i, present) in previews.into_iter().enumerate() {
            let key = battle.battle.mons[i].name.to_lowercase();
            if !present && catalog.0.contains_key(&key) {
                spawn(&mut c, &catalog, &key, Role::Battle(i), preview_origin(i));
            }
        }
    }
    for (mut player, mut transitions, mut playback) in &mut playback {
        let Ok((_, root, _, _)) = roots.get(playback.owner) else { continue };
        if root.motion != playback.current {
            let duration = if p.character_animation { Duration::from_millis(140) } else { Duration::ZERO };
            let active = transitions.play(&mut player, playback.clips[root.motion], duration);
            if root.motion < 3 { active.repeat(); }
            playback.current = root.motion;
            eprintln!("[slice-model] {} {}", root.key, MOTIONS[root.motion]);
        }
        let native = if playback.current < 3 { catalog.0.get(&root.key).map_or(0., |m| m.speeds[playback.current]) } else { 0. };
        let rate = if native > 0. && root.speed > 0.05 { (root.speed / native).clamp(0.6, 2.5) } else { 1. };
        if let Some(active) = player.animation_mut(playback.clips[playback.current]) { active.set_speed(rate); }
        let pause = !p.character_animation || p.menu || (battle.active && !matches!(root.role, Role::Battle(_) | Role::Trainer));
        if pause { player.pause_all(); } else { player.resume_all(); }
        if !p.character_animation {
            if let Some(active) = player.animation_mut(playback.clips[0]) { active.seek_to(0.); }
        }
    }
}
pub(super) fn preview_ready(roots: &Query<&Character>, index: usize, key: &str) -> bool {
    roots.iter().any(|r| matches!(r.role, Role::Battle(i) if i == index) && r.key.eq_ignore_ascii_case(key) && r.ready)
}
pub(super) fn player_ready(roots: &Query<&Character>) -> bool {
    roots.iter().any(|r| matches!(r.role, Role::Player) && r.ready)
}
