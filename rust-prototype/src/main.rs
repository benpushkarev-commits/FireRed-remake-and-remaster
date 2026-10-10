#![windows_subsystem = "windows"]
mod battle_screen;
mod battle_stage;
mod letsgo_material;
mod field_screen;
mod music;
mod models;
mod persistence;
mod presentation;
mod slice_start;
mod story_bot;
mod smoke;
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::window::{CursorGrabMode, CursorOptions, PrimaryWindow};
use bevy::{
    prelude::*,
    render::view::screenshot::{Screenshot, save_to_disk},
    winit::{UpdateMode, WinitSettings},
};
use firered_native_prototype::{Cell, Game, Input, TICK_HZ, World};
use std::path::PathBuf;

#[derive(Resource)]
struct Simulation {
    free: firered_native_prototype::free::Controller,
    free_vector: [f32; 2],
    free_active: bool,
    world: World,
    game: Game,
    displayed: Vec3,
    accumulator: f64,
    controls: Input,
    moving: bool,
    dirty: bool,
}
#[derive(Resource)]
struct Preferences {
    field_scripts: bool,
    actor_scenes: bool,
    red_model: bool,
    character_animation: bool,
    trainer_models: bool,
    pokemon_models: bool,
    letsgo_scenery: bool,
    effects: bool,
    quick_start: bool,
    battle_3d: bool,
    letsgo_shading: bool,
    native_state: bool,
    battle_slice: bool,
    source_art: bool,
    music: bool,
    free_move: bool,
    diagonal: bool,
    third_person: bool,
    captured: bool,
    pitch: f32,
    orbit: bool,
    smooth: bool,
    angle: f32,
    menu: bool,
    reset_pending: bool,
    path: PathBuf,
}
impl Preferences {
    fn load(path: PathBuf) -> Self {
        let ini = std::fs::read_to_string(&path).unwrap_or_default();
        Self {
            field_scripts: ini.contains("field_scripts=1"),
            actor_scenes: ini.contains("actor_scenes=1"),
            red_model: !ini.contains("red_model=0"),
            character_animation: !ini.contains("character_animation=0"),
            trainer_models: !ini.contains("trainer_models=0"),
            pokemon_models: !ini.contains("pokemon_models=0"),
            letsgo_scenery: !ini.contains("letsgo_scenery=0"),
            effects: !ini.contains("effects=0"),
            quick_start: !ini.contains("quick_start=0"),
            battle_3d: !ini.contains("battle_3d=0"),
            letsgo_shading: !ini.contains("letsgo_shading=0"),
            native_state: ini.contains("native_state=1"),
            battle_slice: !ini.contains("battle_slice=0"),
            source_art: !ini.contains("source_textures=0"),
            music: ini.contains("music=1"),
            free_move: !ini.contains("free_movement=0"),
            diagonal: !ini.contains("diagonal_movement=0"),
            third_person: !ini.contains("third_person=0"),
            captured: true,
            pitch: 0.42,
            orbit: !ini.contains("camera_rotation=0"),
            smooth: !ini.contains("smooth_movement=0"),
            angle: 0.,
            menu: false,
            reset_pending: false,
            path,
        }
    }
    fn save(&self) {
        let text = format!(
            "[Prototype]\nfield_scripts={}\nnative_state={}\nbattle_slice={}\nsource_textures={}\nmusic={}\nfree_movement={}\ndiagonal_movement={}\nthird_person={}\ncamera_rotation={}\nsmooth_movement={}\n",
            u8::from(self.field_scripts),
            u8::from(self.native_state),
            u8::from(self.battle_slice),
            u8::from(self.source_art),
            u8::from(self.music),
            u8::from(self.free_move),
            u8::from(self.diagonal),
            u8::from(self.third_person),
            u8::from(self.orbit),
            u8::from(self.smooth)
        );
        let text = format!("{text}actor_scenes={}\nred_model={}\n", u8::from(self.actor_scenes), u8::from(self.red_model));
        let text = format!("{text}character_animation={}\ntrainer_models={}\npokemon_models={}\n", u8::from(self.character_animation), u8::from(self.trainer_models), u8::from(self.pokemon_models));
        let text = format!("{text}letsgo_scenery={}\neffects={}\nquick_start={}\nbattle_3d={}\nletsgo_shading={}\n", u8::from(self.letsgo_scenery), u8::from(self.effects), u8::from(self.quick_start), u8::from(self.battle_3d), u8::from(self.letsgo_shading));
        if let Err(e) = std::fs::write(&self.path, text) {
            eprintln!("Cannot save prototype settings: {e}");
        }
    }
}
#[derive(Resource)]
struct Capture {
    path: Option<String>,
    frame: u32,
}
#[derive(Component)]
struct PlayerPart(Vec3);
#[derive(Component)]
struct SceneCamera;
#[derive(Component)]
struct Hud;
fn cell_position(p: Cell) -> Vec3 {
    Vec3::new(p.0 as f32, 0., p.1 as f32)
}
fn main() {
    let args: Vec<String> = std::env::args().collect();
    let base = std::env::current_exe()
        .unwrap()
        .parent()
        .unwrap()
        .to_path_buf();
    let path = if base.join("data/world.json").exists() {
        base.join("data/world.json")
    } else {
        PathBuf::from("data/world.json")
    };
    let world = match World::load(&path) {
        Ok(w) => w,
        Err(e) => {
            eprintln!("World data error: {e}");
            std::process::exit(1)
        }
    };
    let asset_root = if base.join("assets").is_dir() {
        base.join("assets")
    } else {
        PathBuf::from("assets")
    };
    if args.iter().any(|v| v == "--check") {
        persistence::check_world(&world, &base).expect("Native field startup/integration");
        field_screen::check_walking(&world).expect("Native walking encounters/Repel integration");
        firered_native_prototype::actors::check(&world).expect("Native actors startup/integration");
        for file in world
            .maps
            .iter()
            .map(|m| format!("textures/{}_ground.png", m.id))
            .chain(
                ["roof", "wall", "lab_roof", "lab_wall", "window", "door"]
                    .into_iter()
                    .map(|s| format!("textures/{s}.png")),
            )
            .chain([
                "battle/charmander.png".to_string(),
                "battle/squirtle.png".to_string(),
                "music/pallet.wav".to_string(),
                "music/route1.wav".to_string(),
            ])
        {
            let bytes =
                std::fs::read(asset_root.join(&file)).expect("Packaged original assets missing");
            assert!(
                if file.ends_with(".png") {
                    bytes.starts_with(b"\x89PNG\r\n\x1a\n")
                } else {
                    bytes.starts_with(b"RIFF") && bytes.get(8..12) == Some(b"WAVE")
                },
                "Invalid asset {file}"
            );
        }
        println!(
            "Validated {} maps: collision, spawns, props, NPCs and warp destinations",
            world.maps.len()
        );
        return;
    }
    let capture = args
        .iter()
        .position(|a| a == "--capture")
        .and_then(|i| args.get(i + 1))
        .cloned();
    let smoke = args
        .iter()
        .any(|a| a == "--smoke" || a == "--diagonal-smoke" || a == "--free-smoke");
    let interior = args.iter().any(|a| a == "--interior");
    let field_smoke = args.iter().any(|a| a == "--field-smoke");
    let scene_smoke = args.iter().any(|a| a == "--scene-smoke");
    let field_dialogue = args.iter().any(|a| a == "--field-dialogue");
    let route = field_smoke || field_dialogue || args.iter().any(|a| a == "--route");
    let bot = story_bot::Bot::from_args();
    // QA: --map <id> starts on any imported map (its spawn cell).
    let start_map = args.iter().position(|a| a == "--map").and_then(|i| args.get(i + 1)).cloned();
    let mut game = Game::new(
        &world,
        if let Some(m) = start_map.as_deref() {
            m
        } else if interior {
            "home"
        } else if route {
            "route1"
        } else {
            "pallet"
        },
    );
    if field_smoke || field_dialogue {
        let npc = world
            .map("route1")
            .npcs
            .iter()
            .find(|n| n.script == "Route1_EventScript_MartClerk")
            .expect("Original clerk");
        game.position = npc.at.offset(Cell(0, 1));
        game.facing = Cell(0, -1);
        if field_dialogue {
            game.field_scripts = true;
            game.interact(&world);
        }
    }
    if args.iter().any(|a| a == "--dialogue") {
        game.position = Cell(3, 11);
        game.facing = Cell(0, -1);
        game.interact(&world);
    }
    if args.iter().any(|a| a == "--diagonal-view") {
        game.position = Cell(11, 9);
        game.facing = Cell(1, -1);
    }
    let mut prefs = Preferences::load(base.join("config.ini"));
    if field_smoke || field_dialogue || args.iter().any(|a| a == "--field-scripts") {
        prefs.field_scripts = true;
    }
    if scene_smoke || args.iter().any(|a| a == "--actor-scenes") {
        prefs.actor_scenes = true;
        prefs.field_scripts = true;
    }
    if scene_smoke {
        game.position = Cell(4, 15);
        game.facing = Cell(1, 0);
    }
    let qa = capture.is_some()
        || bot.enabled
        || smoke
        || field_smoke
        || scene_smoke
        || args.iter().any(|a| a.starts_with("--battle"));
    if qa
        && !field_smoke
        && !field_dialogue
        && !scene_smoke
        && !args.iter().any(|a| a == "--field-scripts")
        && !args.iter().any(|a| a == "--actor-scenes")
    {
        prefs.field_scripts = false;
    }
    if qa && !scene_smoke && !args.iter().any(|a| a == "--actor-scenes") {
        prefs.actor_scenes = false;
    }
    if bot.enabled {
        // The bot plays the real story on the grid, like the original.
        prefs.field_scripts = true;
        prefs.actor_scenes = true;
        prefs.quick_start = args.iter().any(|a| a == "--quick-start");
        prefs.free_move = false;
        prefs.third_person = !args.iter().any(|a| a == "--bot-classic"); // same mouse camera as the real game
        prefs.menu = false;
    }
    let mut persistence = persistence::Session::open(
        base.join("native-state.json"),
        prefs.native_state && !qa,
        &mut game,
        &world,
    );
    let pos = cell_position(game.position);
    // (the bot never grabs Ben's mouse)
    prefs.captured = capture.is_none() && !smoke && !field_smoke && !scene_smoke && !bot.enabled;
    if args.iter().any(|a| a == "--classic") || smoke || field_smoke || scene_smoke {
        prefs.third_person = false;
    }
    if smoke {
        prefs.free_move = false;
    }
    let free_smoke = args.iter().any(|a| a == "--free-smoke");
    if free_smoke {
        prefs.third_person = true;
        prefs.free_move = true;
    }
    let diagonal_smoke = args.iter().any(|a| a == "--diagonal-smoke");
    if diagonal_smoke {
        prefs.third_person = true;
        prefs.diagonal = true;
    }
    if args.iter().any(|a| a == "--plain-art") {
        prefs.source_art = false;
    }
    prefs.menu = args.iter().any(|a| a == "--settings");
    if args.iter().any(|a| a == "--orbit") {
        prefs.angle = 2.2;
    }
    let mut free = firered_native_prototype::free::Controller::new(&game);
    if args.iter().any(|a| a == "--free-view") {
        free.position = [10.17, 9.71];
        free.yaw = std::f32::consts::PI + 0.31;
        prefs.angle = 0.31;
    }
    // QA capture helper: --at <x> <z> <yaw> places the free-move player and camera.
    if let Some(i) = args.iter().position(|a| a == "--at") {
        let v: Vec<f32> = args[i + 1..].iter().take(3).filter_map(|a| a.parse().ok()).collect();
        if v.len() == 3 {
            free.position = [v[0], v[1]];
            game.position = Cell(v[0].round() as i32, v[1].round() as i32);
            free.yaw = v[2];
            prefs.angle = v[2] + std::f32::consts::PI;
        }
    }
    let battle_smoke = args.iter().any(|a| a == "--battle-smoke");
    let mut battle = battle_screen::Screen::new(args.iter().any(|a| a == "--battle"));
    if args.iter().any(|a| a == "--battle-turns") {
        battle.active = true;
        battle
            .battle
            .play(&battle.data, 1)
            .expect("Battle fixture AI");
        battle
            .battle
            .play(&battle.data, 0)
            .expect("Battle fixture AI");
    }
    if args
        .iter()
        .any(|a| a == "--battle-win" || a == "--battle-loss")
    {
        battle.active = true;
        let slot = usize::from(args.iter().any(|a| a == "--battle-loss"));
        while battle.battle.winner.is_none() {
            battle
                .battle
                .play(&battle.data, slot)
                .expect("Battle end fixture");
        }
    }
    if battle_smoke {
        prefs.battle_slice = true;
    }
    // ResetMenuAndMonGlobals seeds transient wild RNG on session initialization.
    let field = field_screen::Field::new(persistence.state.rng.next16());
    App::new()
        .insert_resource(field_screen::Replay::new(
            field_smoke || scene_smoke,
            base.join(if scene_smoke {
                "scene-smoke-result.json"
            } else {
                "field-smoke-result.json"
            }),
            scene_smoke,
        ))
        .insert_resource(field)
        .insert_resource(persistence)
        .insert_resource(battle)
        .insert_resource(battle_screen::Replay::new(
            battle_smoke,
            base.join("battle-smoke-result.json"),
        ))
        .insert_resource(Simulation {
            free,
            free_vector: [0., 0.],
            free_active: prefs.free_move && prefs.third_person,
            world,
            game,
            displayed: pos,
            accumulator: 0.,
            controls: Input::default(),
            moving: false,
            dirty: true,
        })
        .insert_resource(music::Music::new(
            asset_root.clone(),
            capture.is_some() || smoke || battle_smoke || field_smoke || scene_smoke,
        ))
        .insert_resource(prefs)
        .insert_resource(bot)
        .insert_resource(Capture {
            path: capture.clone(),
            frame: 0,
        })
        .insert_resource(smoke::Replay::new(
            smoke,
            base.join("smoke-result.json"),
            diagonal_smoke,
            free_smoke,
        ))
        .insert_resource(ClearColor(Color::srgb_u8(137, 201, 225)))
        .init_resource::<presentation::WorldShift>()
        .insert_resource(WinitSettings {
            focused_mode: UpdateMode::Continuous,
            unfocused_mode: UpdateMode::Continuous,
        })
        .add_plugins(
            DefaultPlugins
                .set(AssetPlugin {
                    file_path: asset_root.to_string_lossy().into_owned(),
                    ..default()
                })
                .set(WindowPlugin {
                    primary_window: Some(Window {
                        title: "FireRed Native 3D Prototype | Pallet slice".into(),
                        resolution: (1280, 720).into(),
                        visible: capture.is_none()
                            && !smoke
                            && !battle_smoke
                            && !field_smoke
                            && !scene_smoke,
                        ..default()
                    }),
                    ..default()
                })
                .set(ImagePlugin::default_nearest()),
        )
        // Default features are disabled; glTF scene cloning needs these types
        // registered explicitly, including entity-mapped joints and targets.
        .register_type::<Transform>()
        .register_type::<GlobalTransform>()
        .register_type::<bevy::transform::components::TransformTreeChanged>()
        .register_type::<Visibility>()
        .register_type::<InheritedVisibility>()
        .register_type::<ViewVisibility>()
        .register_type::<Name>()
        .register_type::<ChildOf>()
        .register_type::<Children>()
        .register_type::<Mesh3d>()
        .register_type::<MeshMaterial3d<StandardMaterial>>()
        .register_type::<bevy::camera::primitives::Aabb>()
        .register_type::<bevy::mesh::skinning::SkinnedMesh>()
        .register_type::<AnimationPlayer>()
        .register_type::<bevy::animation::AnimationTarget>()
        .register_type::<bevy::gltf::GltfMeshName>()
        .register_type::<bevy::gltf::GltfMaterialName>()
        .register_type::<bevy::gltf::GltfExtras>()
        .register_type::<bevy::gltf::GltfMeshExtras>()
        .register_type::<bevy::gltf::GltfMaterialExtras>()
        .register_type::<bevy::gltf::GltfSceneExtras>()
        .add_systems(Startup, presentation::setup)
        .add_systems(Startup, models::setup)
        .add_systems(Startup, battle_screen::setup)
        .add_systems(Startup, battle_stage::setup)
        .add_plugins(MaterialPlugin::<letsgo_material::FieldMaterial>::default())
        .init_resource::<letsgo_material::LgImages>()
        .add_systems(Update, (letsgo_material::swap, letsgo_material::toggle, letsgo_material::images, letsgo_material::sky).chain())
        .add_systems(
            Update,
            (
                (story_bot::run, smoke::replay).chain(),
                battle_screen::replay,
                field_screen::replay,
                input,
                (slice_start::update, slice_start::auto).chain(),
                battle_screen::controls,
                advance,
                field_screen::update,
                persistence::update,
                presentation::rebuild,
                presentation::animate,
                presentation::effects,
                (battle_stage::update, battle_stage::camera).chain(),
                presentation::sway,
                models::update,
                music::update,
                hud,
                (battle_screen::draw, battle_stage::hud_style).chain(),
                font_safe_text,
                capture_frame,
            )
                .chain(),
        )
        .run();
}
fn input(
    keys: Res<ButtonInput<KeyCode>>,
    mut s: ResMut<Simulation>,
    mut p: ResMut<Preferences>,
    time: Res<Time>,
    mut exit: MessageWriter<AppExit>,
    mouse: Res<AccumulatedMouseMotion>,
    buttons: Res<ButtonInput<MouseButton>>,
    mut window: Single<(&Window, &mut CursorOptions), With<PrimaryWindow>>,
    mut battle: ResMut<battle_screen::Screen>,
    mut field: ResMut<field_screen::Field>,
    mut session: ResMut<persistence::Session>,
) {
    if keys.just_pressed(KeyCode::F1) || keys.just_pressed(KeyCode::Tab) {
        p.menu = !p.menu;
        p.reset_pending = false;
    }
    if field.active() {
        if p.menu && keys.just_pressed(KeyCode::Escape) {
            p.menu = false;
        }
        if !p.menu {
            s.controls = Input::default();
            s.free_vector = [0., 0.];
            return;
        }
    }
    if keys.just_pressed(KeyCode::Escape) {
        if battle.active && !p.menu {
            if !battle.battle.is_wild || battle.battle.winner.is_some() {
                battle.active = false;
                p.captured = false;
            }
        } else if s.game.dialogue.is_some() {
            s.game.cancel_dialogue();
        } else if p.menu {
            p.menu = false;
        } else if p.third_person && p.captured {
            p.captured = false;
        } else {
            exit.write(AppExit::Success);
        }
    }
    if buttons.just_pressed(MouseButton::Left) && !p.menu {
        p.captured = true;
    }
    if keys.just_pressed(KeyCode::KeyB) && !p.menu && p.battle_slice && !battle.active {
        *battle = battle_screen::Screen::new(true);
    }
    let grab = p.third_person && p.captured && !p.menu && !battle.active && window.0.focused;
    window.1.visible = !grab;
    window.1.grab_mode = if grab {
        CursorGrabMode::Locked
    } else {
        CursorGrabMode::None
    };
    if grab && p.orbit {
        p.angle -= mouse.delta.x * 0.003;
        p.pitch = (p.pitch + mouse.delta.y * 0.003).clamp(0.05, 1.2);
    }
    s.free_vector = [0., 0.];
    s.controls = Input {
        interact: s.controls.interact,
        ..Default::default()
    };
    if p.menu {
        s.controls = Input::default();
        if keys.just_pressed(KeyCode::KeyR) {
            if p.reset_pending {
                match session.reset(&mut s.game, p.native_state) {
                    Ok(()) => {
                        field.reset_world(&mut s);
                        *field = field_screen::Field::new(session.state.rng.next16());
                        battle.active = false;
                        s.free = firered_native_prototype::free::Controller::new(&s.game);
                        s.free_vector = [0., 0.];
                        s.dirty = true;
                        p.menu = false;
                    }
                    Err(e) => session.error = format!("Save reset failed: {e}"),
                }
                p.reset_pending = false;
            } else {
                p.reset_pending = true;
            }
            return;
        }
        if keys.just_pressed(KeyCode::Digit1) {
            p.orbit = !p.orbit;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit2) {
            p.smooth = !p.smooth;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit3) {
            p.third_person = !p.third_person;
            p.captured = true;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit4) {
            p.diagonal = !p.diagonal;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit5) {
            p.free_move = !p.free_move;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit6) {
            p.source_art = !p.source_art;
            s.dirty = true;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit7) {
            p.music = !p.music;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit8) {
            p.battle_slice = !p.battle_slice;
            if !p.battle_slice {
                battle.active = false;
            }
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit9) {
            p.native_state = !p.native_state;
            p.save();
        }
        if keys.just_pressed(KeyCode::Digit0) {
            p.field_scripts = !p.field_scripts;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyM) {
            p.actor_scenes = !p.actor_scenes;
            if p.actor_scenes {
                p.field_scripts = true;
            }
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyV) {
            p.red_model = !p.red_model;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyA) {
            p.character_animation = !p.character_animation;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyT) {
            p.trainer_models = !p.trainer_models;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyP) {
            p.pokemon_models = !p.pokemon_models;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyY) {
            p.quick_start = !p.quick_start;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyH) {
            p.letsgo_shading = !p.letsgo_shading;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyG) {
            p.battle_3d = !p.battle_3d;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyK) {
            p.effects = !p.effects;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyL) {
            p.letsgo_scenery = !p.letsgo_scenery;
            s.dirty = true;
            p.save();
        }
        return;
    }
    if battle.active {
        s.controls = Input::default();
        return;
    }
    if p.orbit {
        let turn = u8::from(keys.pressed(KeyCode::KeyE)) as f32
            - u8::from(keys.pressed(KeyCode::KeyQ)) as f32;
        p.angle += turn * time.delta_secs() * 1.6;
        if keys.just_pressed(KeyCode::KeyR) {
            p.angle = 0.;
            p.pitch = 0.42;
        }
    }
    if keys.just_pressed(KeyCode::Space) || keys.just_pressed(KeyCode::Enter) {
        s.controls.interact = true;
    }
    let held = |a, b| i32::from(keys.pressed(a) || keys.pressed(b));
    let x = held(KeyCode::ArrowRight, KeyCode::KeyD) - held(KeyCode::ArrowLeft, KeyCode::KeyA);
    let z = held(KeyCode::ArrowDown, KeyCode::KeyS) - held(KeyCode::ArrowUp, KeyCode::KeyW);
    s.free_vector = firered_native_prototype::free::camera_vector(
        x,
        z,
        if p.third_person { p.angle } else { 0. },
    );
    s.controls.direction = firered_native_prototype::camera_direction(
        x,
        z,
        if p.third_person { p.angle } else { 0. },
        p.third_person && p.diagonal,
    );
    s.game.allow_diagonal = p.third_person && p.diagonal;
    s.controls.run = keys.pressed(KeyCode::ShiftLeft) || keys.pressed(KeyCode::ShiftRight);
}
fn advance(
    mut s: ResMut<Simulation>,
    p: Res<Preferences>,
    time: Res<Time>,
    replay: Res<smoke::Replay>,
    battle: Res<battle_screen::Screen>,
    scenery: Res<models::Scenery>,
) {
    if p.menu || battle.active {
        return;
    }
    let free_mode = p.free_move && p.third_person;
    if free_mode != s.free_active && !s.game.busy() {
        s.free = firered_native_prototype::free::Controller::new(&s.game);
        s.free_active = free_mode;
    }
    s.accumulator += if replay.enabled {
        1. / TICK_HZ
    } else {
        time.delta_secs_f64().min(0.25)
    };
    while s.accumulator >= 1. / TICK_HZ {
        s.accumulator -= 1. / TICK_HZ;
        let before = s.game.map.clone();
        let controls = s.controls;
        let vector = s.free_vector;
        let active = s.free_active;
        let Simulation {
            game, world, free, ..
        } = &mut *s;
        if active {
            free.tick(game, world, vector, controls.run, controls.interact);
        } else {
            game.tick(world, controls);
        }
        s.controls.interact = false;
        if s.game.map != before {
            s.dirty = true;
        }
    }
    if s.free_active {
        let ground = scenery.ground(p.letsgo_scenery, &s.game.map, s.free.position[0], s.free.position[1]);
        s.displayed = Vec3::new(s.free.position[0], s.free.height + ground, s.free.position[1]);
        s.moving = s.free.moving;
        return;
    }
    s.moving = s.game.busy();
    s.displayed = cell_position(s.game.position);
    s.displayed.y += scenery.ground(p.letsgo_scenery, &s.game.map, s.displayed.x, s.displayed.z);
    if let Some(m) = &s.game.motion {
        if m.target_map == s.game.map {
            let t = m.progress();
            let mut displayed = if p.smooth {
                cell_position(m.from).lerp(cell_position(m.to), t)
            } else {
                cell_position(m.to)
            };
            if m.jump {
                displayed.y = (t * std::f32::consts::PI).sin() * 0.6;
            }
            displayed.y += scenery.ground(p.letsgo_scenery, &s.game.map, displayed.x, displayed.z);
            s.displayed = displayed;
        }
    }
}
fn hud(
    s: Res<Simulation>,
    p: Res<Preferences>,
    session: Res<persistence::Session>,
    mut text: Single<&mut Text, With<Hud>>,
) {
    let mut body = if p.menu {
        format!(
            "SETTINGS\n\n1   Camera rotation: {}\n2   Smooth movement: {}\n3   Third-person mouse camera: {}\n4   Diagonal grid movement: {}\n5   Free movement: {}\n6   Original surface textures: {}\n7   Music: {}\n8   Opening rival battle slice: {}\n9   Native save/load (restart): {}\n0   Original field scripts (partial): {}\nV   Imported Red playable model: {}\n\nTab / F1 / Esc: close     Changes save automatically.",
            if p.orbit { "ON" } else { "OFF" },
            if p.smooth { "ON" } else { "OFF" },
            if p.third_person { "ON" } else { "OFF" },
            if p.diagonal { "ON" } else { "OFF" },
            if p.free_move { "ON" } else { "OFF" },
            if p.source_art { "ON" } else { "OFF" },
            if p.music { "ON" } else { "OFF" },
            if p.battle_slice { "ON" } else { "OFF" },
            if p.native_state { "ON" } else { "OFF" },
            if p.field_scripts { "ON" } else { "OFF" },
            if p.red_model { "ON" } else { "OFF" }
        )
    } else if slice_start::needs_starter(&p, &session) && s.game.dialogue.is_none() {
        format!(
            "{}   |   PLAYABLE SLICE\nChoose your partner:   1  Bulbasaur     2  Charmander     3  Squirtle\nThen explore Pallet Town and Route 1: tall grass has wild Pokemon. Tab: settings",
            s.world.map(&s.game.map).name
        )
    } else if let Some((name, lines, i)) = &s.game.dialogue {
        format!("{name}\n\n{}\n\nSpace / Enter: continue", lines[*i])
    } else {
        format!(
            "{}   |   NATIVE RUST SLICE\nWASD: move   Shift: run   Space: interact   Mouse: look   Esc: cursor   Tab: settings   B: rival battle\n{}",
            s.world.map(&s.game.map).name,
            if s.world.map(&s.game.map).indoor {
                "Explore the rooms. Doors and stairs connect the original maps."
            } else if s.game.map == "route1" {
                "South: Pallet Town. Talk to the Mart clerk. Jump ledges heading south."
            } else {
                "North: Route 1. Enter the houses and lab. Read signs and talk to neighbours."
            }
        )
    };
    if p.menu {
        body.push_str(&format!(
            "\nM: Original actors and map scenes (partial): {}\nA: Character animations: {}   T: Trainer models: {}   P: Pokemon models: {}   L: Let's Go scenery: {}   K: Effects: {}\nY: Slice quick start (pick a starter, skip story): {}   G: 3D battle stage: {}\nH: Let's Go shading (trees, ground, water): {}",
            if p.actor_scenes { "ON" } else { "OFF" },
            if p.character_animation { "ON" } else { "OFF" },
            if p.trainer_models { "ON" } else { "OFF" },
            if p.pokemon_models { "ON" } else { "OFF" },
            if p.letsgo_scenery { "ON" } else { "OFF" },
            if p.effects { "ON" } else { "OFF" },
            if p.quick_start { "ON" } else { "OFF" },
            if p.battle_3d { "ON" } else { "OFF" },
            if p.letsgo_shading { "ON" } else { "OFF" }
        ));
        body.push_str(if p.reset_pending {
            "\nReset all Rust progress? Press R again to confirm. Previous save backed up."
        } else {
            "\nR: reset Rust save and progress (keeps current location)"
        });
    }
    if !session.error.is_empty() {
        body.push_str(&format!("\n{}", session.error));
    }
    if text.0 != body {
        text.0 = body;
    }
}
/// The bundled UI font has no accented glyphs; show "Pokemon" instead of a missing box.
fn font_safe_text(mut q: Query<&mut Text, Changed<Text>>) {
    for mut t in &mut q {
        if !t.0.is_ascii() {
            let fixed = t.0.replace('é', "e").replace('É', "E").replace('…', "...")
                .replace(['“', '”'], "\"").replace(['‘', '’'], "'");
            if fixed != t.0 {
                t.0 = fixed;
            }
        }
    }
}
fn capture_frame(mut c: ResMut<Capture>, mut commands: Commands, mut exit: MessageWriter<AppExit>) {
    if c.path.is_none() {
        return;
    }
    c.frame += 1;
    if c.frame == 160 {
        let path = c.path.clone().unwrap();
        commands
            .spawn(Screenshot::primary_window())
            .observe(save_to_disk(path));
    }
    // Leave enough frames for GPU readback and PNG writing.
    if c.frame == 200 {
        exit.write(AppExit::Success);
    }
}
