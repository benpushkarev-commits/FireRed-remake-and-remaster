//! Simple solid geometry with original FireRed surface artwork. Gameplay depends only on lib.rs and JSON.
use super::*;
use bevy::asset::RenderAssetUsages;
use bevy::camera::{RenderTarget, ScalingMode};
use bevy::core_pipeline::tonemapping::Tonemapping;
use bevy::light::{NotShadowCaster, NotShadowReceiver};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::render::render_resource::TextureFormat;
use firered_native_prototype::Prop;
use std::collections::HashMap;

#[derive(Component)]
pub(super) struct Geometry;
#[derive(Component)]
pub(super) struct NpcPart {
    id: u16,
    offset: Vec3,
    emote: bool,
    model: Option<Entity>,
}
#[derive(Resource)]
pub(super) struct Art {
    cube: Handle<Mesh>,
    kit_grass: Handle<StandardMaterial>,
    kit_path: Handle<StandardMaterial>,
    kit_tree: Handle<Scene>,
    kit_grass_piece: Handle<Scene>,
    kit_flower: Handle<Scene>,
    kit_cliff: Handle<StandardMaterial>,
    kit_stone: Handle<StandardMaterial>,
    colors: Vec<Handle<StandardMaterial>>,
    textures: HashMap<String, Handle<StandardMaterial>>,
}
const GRASS: usize = 0;
const PATH: usize = 1;
const BARK: usize = 2;
const LEAF: usize = 3;
const LEAF_LIGHT: usize = 4;
const WALL: usize = 5;
const ROOF: usize = 6;
const DARK: usize = 7;
const GLASS: usize = 8;
const WHITE: usize = 9;
const WATER: usize = 10;
const WOOD: usize = 11;
const SKIN: usize = 12;
const RED: usize = 13;
const BLUE: usize = 14;
const PINK: usize = 15;
const FLOOR: usize = 16;
const GOLD: usize = 17;
const GRASS_LIGHT: usize = 18;
const ROOF_LIGHT: usize = 19;
const STONE: usize = 21;
thread_local! {
    /// World offset of the map whose geometry is being spawned (neighbours sit beside the current map).
    static ORIGIN: std::cell::Cell<Vec3> = const { std::cell::Cell::new(Vec3::ZERO) };
}
fn origin() -> Vec3 {
    ORIGIN.with(|o| o.get())
}
fn block(c: &mut Commands, a: &Art, p: Vec3, size: Vec3, color: usize) -> Entity {
    c.spawn((
        Geometry,
        Mesh3d(a.cube.clone()),
        MeshMaterial3d(a.colors[color].clone()),
        Transform::from_translation(p + origin()).with_scale(size),
    ))
    .id()
}
fn surface(
    c: &mut Commands,
    meshes: &mut Assets<Mesh>,
    material: Handle<StandardMaterial>,
    points: [Vec3; 4],
) {
    let normal = (points[1] - points[0])
        .cross(points[2] - points[0])
        .normalize();
    let mesh = Mesh::new(
        PrimitiveTopology::TriangleList,
        RenderAssetUsages::default(),
    )
    .with_inserted_attribute(
        Mesh::ATTRIBUTE_POSITION,
        points.map(|p| p.to_array()).to_vec(),
    )
    .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, vec![normal.to_array(); 4])
    .with_inserted_attribute(
        Mesh::ATTRIBUTE_UV_0,
        vec![[0., 1.], [1., 1.], [1., 0.], [0., 0.]],
    )
    .with_inserted_indices(Indices::U32(vec![0, 1, 2, 0, 2, 3]));
    c.spawn((
        Geometry,
        Mesh3d(meshes.add(mesh)),
        MeshMaterial3d(material),
        Transform::from_translation(origin()),
    ));
}
pub(super) fn setup(
    mut c: Commands,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut images: ResMut<Assets<Image>>,
    server: Res<AssetServer>,
    simulation: Res<Simulation>,
) {
    let mut palette = [
        (111, 190, 93),
        (230, 209, 146),
        (129, 92, 54),
        (51, 138, 83),
        (80, 170, 90),
        (250, 230, 169),
        (213, 74, 66),
        (56, 64, 74),
        (111, 206, 234),
        (255, 246, 215),
        (71, 164, 215),
        (171, 116, 65),
        (249, 195, 139),
        (233, 66, 68),
        (65, 101, 157),
        (243, 133, 179),
        (220, 182, 117),
        (254, 218, 96),
        (123, 199, 99),
        (222, 88, 74),
        (79, 119, 171),
        (123, 123, 131),
    ];
    let asset_base = std::env::current_exe()
        .unwrap()
        .parent()
        .unwrap()
        .to_path_buf();
    let art_path = if asset_base.join("data/art.json").is_file() {
        asset_base.join("data/art.json")
    } else {
        PathBuf::from("data/art.json")
    };
    let source: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(art_path).expect("Original palette missing"))
            .expect("Invalid art palette");
    let color = |key: &str, index: usize| {
        let v = &source[key][index];
        (
            v[0].as_u64().unwrap() as u8,
            v[1].as_u64().unwrap() as u8,
            v[2].as_u64().unwrap() as u8,
        )
    };
    for (target, index) in [
        (SKIN, 2),
        (RED, 12),
        (WHITE, 9),
        (BLUE, 5),
        (GOLD, 13),
        (DARK, 6),
    ] {
        palette[target] = color("red_palette", index);
    }
    palette[LEAF] = color("tree_palette", 2);
    palette[LEAF_LIGHT] = color("tree_palette", 1);
    palette[BARK] = color("tree_palette", 5);
    let mut textures = HashMap::new();
    for name in simulation
        .world
        .maps
        .iter()
        .map(|m| format!("{}_ground", m.id))
        .chain(
            ["roof", "wall", "lab_roof", "lab_wall", "window", "door"]
                .into_iter()
                .map(str::to_string),
        )
    {
        textures.insert(
            name.clone(),
            materials.add(StandardMaterial {
                base_color_texture: Some(server.load(format!("textures/{name}.png"))),
                perceptual_roughness: 1.,
                reflectance: 0.,
                ..default()
            }),
        );
    }
    let colors = palette
        .iter()
        .map(|&(r, g, b)| {
            materials.add(StandardMaterial {
                base_color: Color::srgb_u8(r, g, b),
                perceptual_roughness: 1.,
                reflectance: 0.,
                ..default()
            })
        })
        .collect();
    let linear = |s: &mut bevy::image::ImageLoaderSettings| s.sampler = bevy::image::ImageSampler::linear();
    let kit_material = |path: &str, materials: &mut Assets<StandardMaterial>| materials.add(StandardMaterial {
        base_color_texture: Some(server.load_with_settings(path.to_owned(), linear)),
        perceptual_roughness: 1.,
        reflectance: 0.,
        ..default()
    });
    let kit_grass = kit_material("letsgo/kit/grass01_com.png", &mut materials);
    let kit_path = kit_material("letsgo/kit/soil01_com.png", &mut materials);
    let kit_cliff = kit_material("letsgo/kit/cliff01_com.png", &mut materials);
    let kit_stone = kit_material("letsgo/kit/rock01_com.png", &mut materials);
    let scene = |p: &str| server.load(GltfAssetLabel::Scene(0).from_asset(p.to_owned()));
    c.insert_resource(Art {
        cube: meshes.add(Cuboid::new(1., 1., 1.)),
        kit_grass,
        kit_path,
        kit_tree: scene("letsgo/kit/tree.glb"),
        kit_grass_piece: scene("letsgo/kit/grass.glb"),
        kit_flower: scene("letsgo/kit/flower.glb"),
        kit_cliff,
        kit_stone,
        colors,
        textures,
    });
    c.insert_resource(AmbientLight {
        color: Color::WHITE,
        brightness: 350.,
        ..default()
    });
    c.spawn((
        DirectionalLight {
            illuminance: 3400.,
            color: Color::srgb(1., 0.97, 0.9),
            shadows_enabled: true,
            ..default()
        },
        bevy::light::CascadeShadowConfigBuilder {
            num_cascades: 3,
            first_cascade_far_bound: 9.,
            maximum_distance: 70.,
            ..default()
        }
        .build(),
        Transform::from_xyz(-10., 24., 10.).looking_at(Vec3::ZERO, Vec3::Y),
    ));
    // Render the scene at the prototype window resolution to avoid pixelated upscaling.
    // UI renders at window resolution so instructions stay readable.
    let image = images.add(Image::new_target_texture(
        1280,
        720,
        TextureFormat::Rgba8UnormSrgb,
    ));
    c.spawn((
        SceneCamera,
        Camera3d::default(),
        Camera {
            target: RenderTarget::Image(image.clone().into()),
            order: -1,
            ..default()
        },
        Projection::Orthographic(OrthographicProjection {
            scaling_mode: ScalingMode::FixedVertical {
                viewport_height: 19.,
            },
            ..OrthographicProjection::default_3d()
        }),
        Msaa::Off,
        Tonemapping::None,
        bevy::render::view::Hdr,
        Transform::default(),
    ));
    c.spawn((Camera2d, IsDefaultUiCamera));
    c.spawn((
        ImageNode::new(image),
        GlobalZIndex(-100),
        Node {
            position_type: PositionType::Absolute,
            width: percent(100),
            height: percent(100),
            ..default()
        },
    ));
    c.spawn((
        GlobalZIndex(100),
        Node {
            position_type: PositionType::Absolute,
            bottom: px(18),
            left: percent(4),
            width: percent(92),
            padding: UiRect::all(px(16)),
            ..default()
        },
        BackgroundColor(Color::srgba(0.12, 0.19, 0.23, 0.96)),
        BorderRadius::all(px(8)),
    ))
    .with_children(|panel| {
        panel.spawn((
            Hud,
            Text::new(""),
            TextFont {
                font_size: 19.,
                ..default()
            },
            TextColor(Color::srgb_u8(255, 247, 215)),
        ));
    });
}
fn tree(c: &mut Commands, a: &Art, x: f32, z: f32) {
    block(
        c,
        a,
        Vec3::new(x, 0.65, z),
        Vec3::new(0.35, 1.3, 0.35),
        BARK,
    );
    for (height, width, color) in [(1.05, 1.6, LEAF), (1.65, 1.3, LEAF_LIGHT), (2.2, 0.8, LEAF)] {
        block(
            c,
            a,
            Vec3::new(x, height, z),
            Vec3::new(width, 0.75, width),
            color,
        );
    }
}
fn window(c: &mut Commands, a: &Art, x: f32, y: f32, z: f32, side: bool) {
    let frame = if side {
        Vec3::new(0.10, 0.9, 1.05)
    } else {
        Vec3::new(1.05, 0.9, 0.10)
    };
    let pane = if side {
        Vec3::new(0.12, 0.68, 0.82)
    } else {
        Vec3::new(0.82, 0.68, 0.12)
    };
    block(c, a, Vec3::new(x, y, z), frame, WHITE);
    block(c, a, Vec3::new(x, y, z), pane, GLASS);
    block(
        c,
        a,
        Vec3::new(x, y, z),
        if side {
            Vec3::new(0.14, 0.08, 0.82)
        } else {
            Vec3::new(0.82, 0.08, 0.14)
        },
        WHITE,
    );
    block(
        c,
        a,
        Vec3::new(x, y, z),
        if side {
            Vec3::new(0.14, 0.68, 0.08)
        } else {
            Vec3::new(0.08, 0.68, 0.14)
        },
        WHITE,
    );
}
fn building(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, p: &Prop, textured: bool) {
    let w = p.size.0 as f32;
    let d = p.size.1 as f32;
    let x = p.at.0 as f32 + (w - 1.) / 2.;
    let z = p.at.1 as f32 + (d - 1.) / 2.;
    let front = z + d / 2.;
    let door_x = p.door.map(|d| d.0 as f32).unwrap_or(x);
    let back = z - d / 2.;
    block(
        c,
        a,
        Vec3::new(x, 0.15, z),
        Vec3::new(w + 0.1, 0.3, d + 0.1),
        BARK,
    );
    block(c, a, Vec3::new(x, 1.25, z), Vec3::new(w, 2.2, d), WALL);
    // Adjacent closed roof bands avoid thin overlapping trim that flickers when orbiting.
    let band_depth = (d + 0.6) / 6.;
    let roof = meshes.add(Extrusion::new(
        Triangle2d::new(
            Vec2::new(-(w + 0.5) / 2., 0.),
            Vec2::new((w + 0.5) / 2., 0.),
            Vec2::new(0., 1.3),
        ),
        band_depth,
    ));
    for band in 0..6 {
        let color = if p.kind == "lab" {
            STONE
        } else {
            if band % 2 == 0 { ROOF } else { ROOF_LIGHT }
        };
        c.spawn((
            Geometry,
            Mesh3d(roof.clone()),
            NotShadowReceiver,
            MeshMaterial3d(a.colors[color].clone()),
            Transform::from_xyz(
                x,
                2.35,
                z - (d + 0.6) / 2. + band_depth * (band as f32 + 0.5),
            )
            .with_translation(origin() + Vec3::new(x, 2.35, z - (d + 0.6) / 2. + band_depth * (band as f32 + 0.5))),
        ));
    }
    block(
        c,
        a,
        Vec3::new(x, 2.36, z),
        Vec3::new(w + 0.5, 0.14, d + 0.6),
        WHITE,
    );
    block(
        c,
        a,
        Vec3::new(door_x, 0.8, front + 0.04),
        Vec3::new(0.9, 1.6, 0.16),
        DARK,
    );
    block(
        c,
        a,
        Vec3::new(door_x, 0.76, front + 0.13),
        Vec3::new(0.65, 1.48, 0.08),
        WOOD,
    );
    block(
        c,
        a,
        Vec3::new(door_x + 0.21, 0.75, front + 0.19),
        Vec3::splat(0.1),
        GOLD,
    );
    for wx in [x - w * 0.30, x + w * 0.30] {
        window(c, a, wx, 1.45, front + 0.06, false);
        window(c, a, wx, 1.45, back - 0.06, false);
    }
    for sx in [x - w / 2. - 0.06, x + w / 2. + 0.06] {
        window(c, a, sx, 1.45, z, true);
    }
    block(
        c,
        a,
        Vec3::new(door_x, 0.08, front + 0.55),
        Vec3::new(1.3, 0.16, 0.95),
        PATH,
    );
    if textured {
        let wall = a.textures[if p.kind == "lab" { "lab_wall" } else { "wall" }].clone();
        let left = x - w / 2. - 0.006;
        let right = x + w / 2. + 0.006;
        let near = front + 0.006;
        let far = back - 0.006;
        surface(
            c,
            meshes,
            wall.clone(),
            [
                Vec3::new(left, 0.3, near),
                Vec3::new(right, 0.3, near),
                Vec3::new(right, 2.32, near),
                Vec3::new(left, 2.32, near),
            ],
        );
        surface(
            c,
            meshes,
            wall.clone(),
            [
                Vec3::new(right, 0.3, far),
                Vec3::new(left, 0.3, far),
                Vec3::new(left, 2.32, far),
                Vec3::new(right, 2.32, far),
            ],
        );
        surface(
            c,
            meshes,
            wall.clone(),
            [
                Vec3::new(left, 0.3, far),
                Vec3::new(left, 0.3, near),
                Vec3::new(left, 2.32, near),
                Vec3::new(left, 2.32, far),
            ],
        );
        surface(
            c,
            meshes,
            wall,
            [
                Vec3::new(right, 0.3, near),
                Vec3::new(right, 0.3, far),
                Vec3::new(right, 2.32, far),
                Vec3::new(right, 2.32, near),
            ],
        );
        let roof = a.textures[if p.kind == "lab" { "lab_roof" } else { "roof" }].clone();
        let edge = (w + 0.5) / 2.;
        let f = z + (d + 0.6) / 2. + 0.005;
        let b = z - (d + 0.6) / 2. - 0.005;
        surface(
            c,
            meshes,
            roof.clone(),
            [
                Vec3::new(x - edge, 2.365, f),
                Vec3::new(x, 3.665, f),
                Vec3::new(x, 3.665, b),
                Vec3::new(x - edge, 2.365, b),
            ],
        );
        surface(
            c,
            meshes,
            roof,
            [
                Vec3::new(x, 3.665, f),
                Vec3::new(x + edge, 2.365, f),
                Vec3::new(x + edge, 2.365, b),
                Vec3::new(x, 3.665, b),
            ],
        );
        let win = a.textures["window"].clone();
        for wx in [x - w * 0.3, x + w * 0.3] {
            surface(
                c,
                meshes,
                win.clone(),
                [
                    Vec3::new(wx - 0.41, 1.11, front + 0.14),
                    Vec3::new(wx + 0.41, 1.11, front + 0.14),
                    Vec3::new(wx + 0.41, 1.79, front + 0.14),
                    Vec3::new(wx - 0.41, 1.79, front + 0.14),
                ],
            );
            surface(
                c,
                meshes,
                win.clone(),
                [
                    Vec3::new(wx + 0.41, 1.11, back - 0.14),
                    Vec3::new(wx - 0.41, 1.11, back - 0.14),
                    Vec3::new(wx - 0.41, 1.79, back - 0.14),
                    Vec3::new(wx + 0.41, 1.79, back - 0.14),
                ],
            );
        }
        surface(
            c,
            meshes,
            a.textures["door"].clone(),
            [
                Vec3::new(door_x - 0.325, 0.02, front + 0.18),
                Vec3::new(door_x + 0.325, 0.02, front + 0.18),
                Vec3::new(door_x + 0.325, 1.5, front + 0.18),
                Vec3::new(door_x - 0.325, 1.5, front + 0.18),
            ],
        );
    }
    // Door cell is in front of the solid footprint, keeping geometry and collision aligned.
    if p.kind == "house" && p.at.0 == 4 {
        block(
            c,
            a,
            Vec3::new(x, 0.012, front + 0.52),
            Vec3::new(0.8, 0.025, 0.65),
            RED,
        );
    }
}
fn furniture(c: &mut Commands, a: &Art, p: &Prop) {
    let x = p.at.0 as f32 + (p.size.0 - 1) as f32 / 2.;
    let z = p.at.1 as f32 + (p.size.1 - 1) as f32 / 2.;
    let w = p.size.0 as f32;
    let d = p.size.1 as f32;
    match p.kind.as_str() {
        "table" => {
            block(
                c,
                a,
                Vec3::new(x, 0.95, z),
                Vec3::new(w - 0.15, 0.18, d - 0.15),
                WOOD,
            );
            for sx in [-1., 1.] {
                for sz in [-1., 1.] {
                    block(
                        c,
                        a,
                        Vec3::new(x + sx * (w / 2. - 0.3), 0.45, z + sz * (d / 2. - 0.3)),
                        Vec3::new(0.2, 0.9, 0.2),
                        BARK,
                    );
                }
            }
            block(
                c,
                a,
                Vec3::new(x, 1.08, z),
                Vec3::new(0.5, 0.12, 0.7),
                WHITE,
            );
        }
        "sofa" => {
            block(
                c,
                a,
                Vec3::new(x, 0.35, z),
                Vec3::new(w - 0.15, 0.7, d - 0.15),
                BLUE,
            );
            block(
                c,
                a,
                Vec3::new(x - w / 2. + 0.2, 0.85, z),
                Vec3::new(0.3, 1., d),
                BLUE,
            );
            for end in [-1., 1.] {
                block(
                    c,
                    a,
                    Vec3::new(x, 0.75, z + end * (d / 2. - 0.15)),
                    Vec3::new(w, 0.55, 0.3),
                    GLASS,
                );
            }
        }
        "bed" => {
            block(
                c,
                a,
                Vec3::new(x, 0.25, z),
                Vec3::new(w - 0.1, 0.5, d - 0.1),
                WOOD,
            );
            block(
                c,
                a,
                Vec3::new(x, 0.58, z),
                Vec3::new(w - 0.2, 0.22, d - 0.2),
                PINK,
            );
            block(
                c,
                a,
                Vec3::new(x, 0.75, z - d / 2. + 0.5),
                Vec3::new(w - 0.4, 0.18, 0.6),
                WHITE,
            );
            block(
                c,
                a,
                Vec3::new(x, 0.8, z - d / 2. + 0.12),
                Vec3::new(w, 1.3, 0.22),
                WOOD,
            );
        }
        "plant" => {
            block(c, a, Vec3::new(x, 0.25, z), Vec3::new(0.6, 0.5, 0.6), ROOF);
            block(c, a, Vec3::new(x, 0.7, z), Vec3::new(0.2, 0.8, 0.2), BARK);
            block(c, a, Vec3::new(x, 1.1, z), Vec3::new(0.9, 0.8, 0.9), LEAF);
        }
        _ => {}
    }
}
fn character(c: &mut Commands, a: &Art, pos: Vec3, player: bool, npc: u16, model: Option<Entity>) {
    let parts = [
        (
            Vec3::new(-0.17, 0.16, 0.),
            Vec3::new(0.23, 0.3, 0.3),
            if player { BLUE } else { DARK },
        ),
        (
            Vec3::new(0.17, 0.16, 0.),
            Vec3::new(0.23, 0.3, 0.3),
            if player { BLUE } else { DARK },
        ),
        (
            Vec3::new(0., 0.50, 0.),
            Vec3::new(0.52, 0.42, 0.32),
            if player { RED } else { PINK },
        ),
        (Vec3::new(-0.35, 0.49, 0.), Vec3::new(0.16, 0.35, 0.2), SKIN),
        (Vec3::new(0.35, 0.49, 0.), Vec3::new(0.16, 0.35, 0.2), SKIN),
        (Vec3::new(0., 0.9, 0.), Vec3::new(0.49, 0.4, 0.4), SKIN),
        (
            Vec3::new(0., 1.12, -0.02),
            Vec3::new(0.56, 0.14, 0.46),
            if player { RED } else { BARK },
        ),
        (
            Vec3::new(0., 1.08, 0.23),
            Vec3::new(0.56, 0.07, 0.18),
            if player { WHITE } else { BARK },
        ),
        (
            Vec3::new(-0.11, 0.91, 0.207),
            Vec3::new(0.065, 0.07, 0.025),
            DARK,
        ),
        (
            Vec3::new(0.11, 0.91, 0.207),
            Vec3::new(0.065, 0.07, 0.025),
            DARK,
        ),
        (
            Vec3::new(0., 0.57, -0.23),
            Vec3::new(0.34, 0.35, 0.16),
            GOLD,
        ),
    ];
    for (offset, size, color) in parts {
        let id = block(c, a, pos + offset, size, color);
        if player {
            c.entity(id).insert(PlayerPart(offset));
        } else {
            c.entity(id).insert(NpcPart {
                id: npc,
                offset,
                emote: false,
                model,
            });
        }
    }
    let shadow = block(
        c,
        a,
        pos + Vec3::new(0., 0.008, 0.),
        Vec3::new(0.65, 0.015, 0.48),
        BARK,
    );
    c.entity(shadow).insert(NotShadowCaster);
    if player {
        c.entity(shadow)
            .insert(PlayerPart(Vec3::new(0., 0.008, 0.)));
    } else {
        c.entity(shadow).insert(NpcPart {
            id: npc,
            offset: Vec3::new(0., 0.008, 0.),
            emote: false,
            model, // the imported model casts a real shadow; hide the block plate with it
        });
        for (offset, size) in [
            (Vec3::new(0., 1.7, 0.), Vec3::new(0.1, 0.3, 0.1)),
            (Vec3::new(0., 1.46, 0.), Vec3::splat(0.1)),
        ] {
            let mark = block(c, a, pos + offset, size, WHITE);
            c.entity(mark).insert((
                NpcPart {
                    id: npc,
                    offset,
                    emote: true,
                    model: None,
                },
                Visibility::Hidden,
            ));
        }
    }
}
/// FireRed block scenery for one map, offset by the current ORIGIN.
fn spawn_map_blocks(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, m: &firered_native_prototype::Map, preferences: &Preferences, kit: &models::Kit, ground: Option<&models::Heights>) {
    // On Let's Go terrain (overlay maps) the ground itself comes from the Let's Go scene;
    // only FireRed features are dressed, standing on the sampled terrain height.
    let terrain = ground.is_some();
    let lift = |x: f32, z: f32| ground.map_or(0., |g| g.at(x, z));
    let lg_tree_near = |x: f32, z: f32| ground.is_some_and(|g| g.trees.iter().any(|t| t.distance(Vec2::new(x, z)) < 1.6));
    // Let's Go kit pieces dress maps that have no matching Let's Go layout.
    let kit = kit.ready && preferences.letsgo_scenery && !m.indoor;
    let inside = m.indoor;
    let width = m.tiles[0].len() as f32;
    let depth = m.tiles.len() as f32;
    if !terrain {
    block(
        c,
        &a,
        Vec3::new((width - 1.) / 2., -0.26, (depth - 1.) / 2.),
        Vec3::new(width, 0.5, depth),
        if inside { BARK } else { GRASS },
    );
    }
    if preferences.source_art && !kit {
        surface(
            c,
            meshes,
            a.textures[&format!("{}_ground", m.id)].clone(),
            [
                Vec3::new(-0.5, 0.018, depth - 0.5),
                Vec3::new(width - 0.5, 0.018, depth - 0.5),
                Vec3::new(width - 0.5, 0.018, -0.5),
                Vec3::new(-0.5, 0.018, -0.5),
            ],
        );
    }
    for (z, row) in m.tiles.iter().enumerate() {
        for (x, tile) in row.bytes().enumerate() {
            let p = Vec3::new(x as f32, 0., z as f32);
            let color = if inside {
                FLOOR
            } else {
                match tile {
                    b'p' => PATH,
                    b'W' => WATER,
                    _ => {
                        if (x * 13 + z * 7) % 5 == 0 {
                            GRASS_LIGHT
                        } else {
                            GRASS
                        }
                    }
                }
            };
            if terrain {
                // Let's Go terrain already covers the ground
            } else if kit && !inside && tile != b'W' {
                let mat = if tile == b'p' { a.kit_path.clone() } else { a.kit_grass.clone() };
                c.spawn((Geometry, Mesh3d(a.cube.clone()), MeshMaterial3d(mat),
                    Transform::from_translation(p + Vec3::new(0., -0.015, 0.) + origin()).with_scale(Vec3::new(1., 0.04, 1.))));
            } else {
                block(
                    c,
                    &a,
                    p + Vec3::new(0., -0.015, 0.),
                    Vec3::new(1., 0.04, 1.),
                    color,
                );
            }
            if inside {
                if tile == b'X' {
                    // All four sides exist; front and side walls are low cutaways for playability.
                    let height = if z == 0 { 2.8 } else { 0.55 };
                    block(
                        c,
                        &a,
                        p + Vec3::new(0., height / 2., 0.),
                        Vec3::new(1., height, 1.),
                        WALL,
                    );
                    block(
                        c,
                        &a,
                        p + Vec3::new(0., height + 0.03, 0.),
                        Vec3::new(1., 0.08, 1.),
                        WOOD,
                    );
                } else {
                    block(
                        c,
                        &a,
                        p + Vec3::new(0., 0.011, 0.45),
                        Vec3::new(1., 0.015, 0.035),
                        WOOD,
                    );
                }
            } else {
                let cell = Cell(x as i32, z as i32);
                let mid = m.cell(cell) & 0x3ff;
                let covered = m.props.iter().any(|v| {
                    ["house", "lab", "sign"].contains(&v.kind.as_str())
                        && cell.0 >= v.at.0
                        && cell.0 < v.at.0 + v.size.0
                        && cell.1 >= v.at.1
                        && cell.1 < v.at.1 + v.size.1
                });
                let tree_tile = [
                    0x0e, 0x0f, 0x1c, 0x1d, 0x1e, 0x1f, 0x14, 0x15, 0x16, 0x17, 0x24, 0x25, 0x26,
                    0x27, 0x2aa, 0x2ab, 0x2b2, 0x2ba,
                ]
                .contains(&mid);
                if tree_tile && !covered {
                    if x % 2 == 0 && z % 2 == 0 {
                        let (tx, tz) = (x as f32 + 0.5, z as f32 + 0.5);
                        if terrain && lg_tree_near(tx, tz) {
                            // a Let's Go tree already stands here
                        } else if kit {
                            piece(c, &a.kit_tree, Vec3::new(tx, lift(tx, tz), tz), x * 31 + z * 17, 0.8, 0.012);
                        } else {
                            tree(c, &a, x as f32 + 0.5, z as f32 + 0.5);
                        }
                    }
                } else if tile == b'X' && !covered && kit {
                    c.spawn((Geometry, Mesh3d(a.cube.clone()), MeshMaterial3d(a.kit_stone.clone()),
                        Transform::from_translation(p + Vec3::Y * (0.25 + lift(p.x, p.z)) + origin()).with_scale(Vec3::new(0.98, 0.5, 0.98))));
                } else if tile == b'X' && !covered {
                    block(
                        c,
                        &a,
                        p + Vec3::Y * 0.25,
                        Vec3::new(0.98, 0.5, 0.98),
                        if [0xe7, 0xec, 0xed, 0x284, 0x287].contains(&mid) {
                            WHITE
                        } else {
                            BARK
                        },
                    );
                }
                if tile == b'L' && kit {
                    c.spawn((Geometry, Mesh3d(a.cube.clone()), MeshMaterial3d(a.kit_cliff.clone()),
                        Transform::from_translation(p + Vec3::Y * (0.12 + lift(p.x, p.z)) + origin()).with_scale(Vec3::new(1., 0.24, 0.8))));
                } else if tile == b'L' {
                    block(
                        c,
                        &a,
                        p + Vec3::Y * 0.12,
                        Vec3::new(1., 0.24, 0.8),
                        BARK,
                    );
                }
                if tile == b'g' && kit {
                    piece(c, &a.kit_grass_piece, p + Vec3::Y * lift(p.x, p.z), x * 7 + z * 13, 1., 0.07);
                } else if tile == b'g' {
                    for dx in [-0.28, 0., 0.28] {
                        block(
                            c,
                            &a,
                            p + Vec3::new(dx, 0.12, 0.),
                            Vec3::new(0.12, 0.24, 0.65),
                            LEAF,
                        );
                    }
                }
                if tile == b'f' && kit {
                    piece(c, &a.kit_flower, p + Vec3::Y * lift(p.x, p.z), x * 11 + z * 5, 1., 0.04);
                } else if tile == b'f' {
                    for dx in [-0.22, 0.22] {
                        block(
                            c,
                            &a,
                            p + Vec3::new(dx, 0.11, 0.),
                            Vec3::new(0.18, 0.12, 0.18),
                            PINK,
                        );
                        block(
                            c,
                            &a,
                            p + Vec3::new(dx, 0.16, 0.),
                            Vec3::new(0.08, 0.03, 0.08),
                            GOLD,
                        );
                    }
                }
                if tile == b'W' && (x + z) % 3 == 0 {
                    let wave = block(
                        c,
                        &a,
                        p + Vec3::new(0., 0.018, 0.),
                        Vec3::new(0.45, 0.015, 0.08),
                        GLASS,
                    );
                    c.entity(wave).insert((NotShadowCaster, NotShadowReceiver));
                }
            }
        }
    }
    for p in &m.props {
        match p.kind.as_str() {
            "house" | "lab" => building(c, &a, meshes, p, preferences.source_art),
            "fence" => {
                for dx in 0..p.size.0 {
                    let x = (p.at.0 + dx) as f32;
                    let z = p.at.1 as f32;
                    block(
                        c,
                        &a,
                        Vec3::new(x, 0.5, z),
                        Vec3::new(0.17, 1., 0.18),
                        WHITE,
                    );
                    for y in [0.4, 0.75] {
                        block(
                            c,
                            &a,
                            Vec3::new(x, y, z),
                            Vec3::new(1., 0.12, 0.14),
                            WHITE,
                        );
                    }
                }
            }
            "sign" => {
                let p = cell_position(p.at);
                block(
                    c,
                    &a,
                    p + Vec3::new(0., 0.4, 0.),
                    Vec3::new(0.15, 0.8, 0.15),
                    BARK,
                );
                block(
                    c,
                    &a,
                    p + Vec3::new(0., 0.95, 0.),
                    Vec3::new(0.9, 0.55, 0.15),
                    WOOD,
                );
                for y in [0.85, 1.02] {
                    block(
                        c,
                        &a,
                        p + Vec3::new(0., y, 0.085),
                        Vec3::new(0.55, 0.045, 0.02),
                        DARK,
                    );
                }
            }
            _ => furniture(c, &a, p),
        }
    }
}
fn piece(c: &mut Commands, scene: &Handle<Scene>, at: Vec3, seed: usize, size: f32, sway: f32) {
    // Small deterministic yaw / scale jitter so repeated pieces read as natural.
    let yaw = (seed % 12) as f32 * 0.52;
    let scale = size * (0.92 + (seed % 5) as f32 * 0.04);
    let rotation = Quat::from_rotation_y(yaw);
    c.spawn((Geometry, SceneRoot(scene.clone()), Sway { base: rotation, phase: (seed % 97) as f32 * 0.37, amount: sway },
        Transform::from_translation(at + origin()).with_rotation(rotation).with_scale(Vec3::splat(scale))));
}
/// Gentle wind on kit foliage (pivot at the base of each piece).
#[derive(Component)]
pub(super) struct Sway {
    base: Quat,
    phase: f32,
    amount: f32,
}
pub(super) fn sway(time: Res<Time>, p: Res<Preferences>, mut q: Query<(&Sway, &mut Transform)>) {
    if !p.effects {
        return;
    }
    let t = time.elapsed_secs();
    for (s, mut transform) in &mut q {
        let a = (t * 1.4 + s.phase).sin() * s.amount + (t * 2.3 + s.phase * 1.7).sin() * s.amount * 0.35;
        transform.rotation = s.base * Quat::from_rotation_x(a) * Quat::from_rotation_z(a * 0.6);
    }
}
/// Bloom, tonemapping, anti-aliasing and distance haze on the scene camera (Settings K).
pub(super) fn effects(
    mut c: Commands,
    p: Res<Preferences>,
    s: Res<Simulation>,
    camera: Single<Entity, With<SceneCamera>>,
    mut tonemap: Query<&mut Tonemapping, With<SceneCamera>>,
    mut state: Local<Option<(bool, bool)>>,
) {
    let indoor = s.world.map(&s.game.map).indoor;
    let want = (p.effects, indoor);
    if *state == Some(want) {
        return;
    }
    *state = Some(want);
    let mut e = c.entity(*camera);
    if let Ok(mut t) = tonemap.single_mut() {
        // Let's Go reads bright and saturated: no filmic curve unless asked for (FRLG_TONEMAP=aces).
        *t = if p.effects && std::env::var("FRLG_TONEMAP").is_ok_and(|v| v == "aces") { Tonemapping::AcesFitted } else { Tonemapping::None };
    }
    if p.effects {
        e.insert((
            bevy::post_process::bloom::Bloom { intensity: 0.08, ..bevy::post_process::bloom::Bloom::NATURAL },
            bevy::anti_alias::fxaa::Fxaa::default(),
        ));
        if indoor {
            e.remove::<DistanceFog>();
        } else {
            e.insert(DistanceFog {
                color: Color::srgb_u8(170, 214, 235),
                falloff: FogFalloff::Linear { start: 28., end: 120. },
                ..default()
            });
        }
    } else {
        e.remove::<(bevy::post_process::bloom::Bloom, bevy::anti_alias::fxaa::Fxaa, DistanceFog)>();
    }
}
#[derive(Component)]
pub(super) struct LetsGoScene(String, usize);
/// (offset of the previous map in the current map's frame, previous map id)
#[derive(Resource, Default)]
pub(super) struct WorldShift(pub Vec3, pub String);
/// Connected outdoor maps around `start` with their offsets in start's tile frame.
pub(super) fn neighbourhood(world: &firered_native_prototype::World, start: &str, depth: usize) -> Vec<(String, Vec3)> {
    let mut out = vec![(start.to_string(), Vec3::ZERO)];
    let mut level = vec![0usize];
    let mut i = 0;
    while i < out.len() {
        let (id, o) = out[i].clone();
        let d = level[i];
        i += 1;
        let m = world.map(&id);
        if d >= depth || m.indoor {
            continue;
        }
        for c in &m.connections {
            if out.iter().any(|(k, _)| *k == c.map) {
                continue;
            }
            let t = world.map(&c.map);
            let off = match c.direction.as_str() {
                "up" => Vec3::new(c.offset as f32, 0., -(t.height() as f32)),
                "down" => Vec3::new(c.offset as f32, 0., m.height() as f32),
                "left" => Vec3::new(-(t.width() as f32), 0., c.offset as f32),
                _ => Vec3::new(m.width() as f32, 0., c.offset as f32),
            };
            out.push((c.map.clone(), o + off));
            level.push(d + 1);
        }
    }
    out
}
pub(super) fn rebuild(
    mut c: Commands,
    mut s: ResMut<Simulation>,
    a: Res<Art>,
    mut meshes: ResMut<Assets<Mesh>>,
    old: Query<Entity, With<Geometry>>,
    preferences: Res<Preferences>,
    catalog: Res<models::Catalog>,
    scenery: Res<models::Scenery>,
    mut scenes: Query<(Entity, &LetsGoScene, &mut Transform)>,
    mut shift: ResMut<WorldShift>,
    kit: Res<models::Kit>,
) {
    if !s.dirty {
        return;
    }
    s.dirty = false;
    for e in &old {
        c.entity(e).despawn();
    }
    let m = s.world.map(&s.game.map);
    let inside = m.indoor;
    // Outdoor maps stream in with their connected neighbours so walking between them has no
    // loading zone; only the current map carries NPCs and gameplay. Let's Go scenery replaces
    // the cosmetic blocks; tiles, props and NPCs stay authoritative.
    let maps = if inside { vec![(m.id.clone(), Vec3::ZERO)] } else { neighbourhood(&s.world, &m.id, 2) };
    if let Some((_, back)) = maps.iter().find(|(id, _)| *id == shift.1) {
        shift.0 = *back;   // where the previous map now sits; camera and models carry over
    } else {
        shift.0 = Vec3::ZERO;
    }
    shift.1 = m.id.clone();
    let mut kept = Vec::new();
    for (e, scene, mut transform) in &mut scenes {
        match maps.iter().find(|(id, _)| *id == scene.0) {
            Some((_, o)) if preferences.letsgo_scenery => {
                transform.translation = *o;
                kept.push((scene.0.clone(), scene.1));
            }
            _ => c.entity(e).despawn(),
        }
    }
    for (id, o) in &maps {
        let map = s.world.map(id);
        let lg = preferences.letsgo_scenery && !map.indoor && scenery.scenes.contains_key(id.as_str());
        if lg {
            for (i, h) in scenery.scenes[id.as_str()].iter().enumerate() {
                if !kept.iter().any(|(k, j)| k == id && *j == i) {
                    c.spawn((SceneRoot(h.clone()), Transform::from_translation(*o), LetsGoScene(id.clone(), i)));
                }
            }
        }
        if !lg || scenery.overlay.contains(id.as_str()) {
            ORIGIN.with(|v| v.set(*o));
            let ground = if lg { scenery.heights.get(id.as_str()) } else { None };
            spawn_map_blocks(&mut c, &a, &mut meshes, map, &preferences, &kit, ground);
            ORIGIN.with(|v| v.set(Vec3::ZERO));
        }
    }
    for n in &m.npcs {
        if n.invisible {
            continue;
        }
        let model = models::npc_key(&n.graphics).map(|key| {
            let entity = models::spawn(&mut c, &catalog, key, models::Role::Npc(n.local_id), cell_position(n.at) + Vec3::Y * (0.025 + scenery.ground(preferences.letsgo_scenery, &m.id, n.at.0 as f32, n.at.1 as f32)));
            c.entity(entity).insert(Geometry);
            entity
        });
        character(&mut c, &a, cell_position(n.at), false, n.local_id, model);
    }
    character(&mut c, &a, s.displayed, true, 0, None);
}
pub(super) fn animate(
    s: Res<Simulation>,
    p: Res<Preferences>,
    models: Query<&models::Character>,
    mut actors: Query<
        (&PlayerPart, &mut Transform, &mut Visibility),
        (With<PlayerPart>, Without<SceneCamera>),
    >,
    mut npcs: Query<
        (&NpcPart, &mut Transform, &mut Visibility),
        (With<NpcPart>, Without<PlayerPart>, Without<SceneCamera>),
    >,
    field: Res<field_screen::Field>,
    mut camera: Single<
        (&mut Transform, &mut Projection),
        With<SceneCamera>,
    >,
    time: Res<Time>,
    mut rig: Local<(Option<Vec3>, f32, String)>,
    shift: Res<WorldShift>,
    scenery: Res<models::Scenery>,
) {
    if shift.is_changed() {
        // Crossing into a connected map re-bases coordinates; carry the camera across.
        if let Some(t) = rig.0.as_mut() {
            *t += shift.0;
        }
    }
    for (part, mut transform, mut visible) in &mut npcs {
        if !part.emote {
            *visible = if p.trainer_models && part.model.is_some_and(|e| models.get(e).is_ok_and(|m| m.ready)) {
                Visibility::Hidden
            } else { Visibility::Inherited };
        }
        if part.emote {
            *visible = if field
                .actors
                .actors
                .iter()
                .any(|a| a.object.local_id == part.id && a.emote > 0)
            {
                Visibility::Inherited
            } else {
                Visibility::Hidden
            };
        }
        if let Some((at, facing)) = field.actors.displayed(part.id) {
            let rotation = Quat::from_rotation_y((facing.0 as f32).atan2(facing.1 as f32));
            transform.translation = Vec3::new(at[0], 0., at[1]) + rotation * part.offset;
            transform.rotation = rotation;
        }
    }
    let yaw = if s.free_active && !s.game.script_locked {
        s.free.yaw
    } else {
        (s.game.facing.0 as f32).atan2(s.game.facing.1 as f32)
    };
    let rotation = Quat::from_rotation_y(yaw);
    for (part, mut t, mut visibility) in &mut actors {
        t.translation = s.displayed + rotation * part.0;
        t.rotation = rotation;
        *visibility = if p.red_model && models::player_ready(&models) {
            Visibility::Hidden
        } else {
            Visibility::Inherited
        };
    }
    let m = s.world.map(&s.game.map);
    if p.third_person {
        // Third-person rig: the look-at point trails the player slightly, the boom
        // snaps in against scenery and eases back out, like a normal 3D action game.
        let dt = time.delta_secs().min(0.1);
        let goal = s.displayed + Vec3::Y * 1.15;
        let reset = rig.0.is_none_or(|t| t.distance(goal) > 3.);
        let target = if reset { goal } else { rig.0.unwrap().lerp(goal, 1. - (-14. * dt).exp()) };
        rig.0 = Some(target);
        rig.2 = s.game.map.clone();
        let direction = Vec3::new(
            p.angle.sin() * p.pitch.cos(),
            p.pitch.sin(),
            p.angle.cos() * p.pitch.cos(),
        );
        // Shorten the boom against solid scenery, so the camera cannot sit inside buildings.
        let mut distance = 4.6;
        for step in 3..47 {
            let t = step as f32 * 0.1;
            let point = target + direction * t;
            let cell = Cell(point.x.round() as i32, point.z.round() as i32);
            let ground = scenery.ground(p.letsgo_scenery, &s.game.map, point.x, point.z);
            let up = point.y - ground;
            let building = m.props.iter().any(|v| {
                ["house", "lab"].contains(&v.kind.as_str())
                    && point.x >= v.at.0 as f32 - 0.7
                    && point.x <= (v.at.0 + v.size.0) as f32 - 0.3
                    && point.z >= v.at.1 as f32 - 0.7
                    && point.z <= (v.at.1 + v.size.1) as f32 - 0.3
                    && up < 3.9
            });
            // Outdoors only tall blocking tiles (trees, walls) stop the boom; ledges and water don't.
            let tall = m.indoor || (m.in_bounds(cell) && m.tiles[cell.1 as usize].as_bytes()[cell.0 as usize] == b'X');
            let solid = m.in_bounds(cell)
                && tall
                && !m.terrain_walkable(cell)
                && up
                    < if m.indoor {
                        if cell.1 < 2 { 2.8 } else { 0.65 }
                    } else {
                        3.6
                    };
            if building || solid {
                distance = (t - 0.25).max(0.25);
                break;
            }
        }
        let boom = if reset || distance < rig.1 { distance } else { rig.1 + (distance - rig.1) * (1. - (-3.5 * dt).exp()) };
        rig.1 = boom;
        // Ride over Let's Go terrain instead of collapsing into the player on slopes.
        let mut eye = target + direction * boom;
        let floor = scenery.ground(p.letsgo_scenery, &s.game.map, eye.x, eye.z) + 0.6;
        eye.y = eye.y.max(floor);
        *camera.0 = Transform::from_translation(eye).looking_at(target, Vec3::Y);
        *camera.1 = Projection::Perspective(PerspectiveProjection {
            fov: 60_f32.to_radians(),
            near: 0.05,
            ..default()
        });
        return;
    }
    if !matches!(&*camera.1, Projection::Orthographic(_)) {
        *camera.1 = Projection::Orthographic(OrthographicProjection::default_3d());
    }
    let target = if m.indoor {
        Vec3::new(
            (m.width() as f32 - 1.) / 2.,
            0.4,
            (m.height() as f32 - 1.) / 2.,
        )
    } else {
        s.displayed + Vec3::Y * 0.4
    };
    let offset = Vec3::new(p.angle.sin() * 22., 24., p.angle.cos() * 22.);
    *camera.0 = Transform::from_translation(target + offset).looking_at(target, Vec3::Y);
    if let Projection::Orthographic(o) = &mut *camera.1 {
        o.scaling_mode = ScalingMode::FixedVertical {
            viewport_height: if m.indoor {
                m.height() as f32 + 4.
            } else {
                19.
            },
        };
    }
}

