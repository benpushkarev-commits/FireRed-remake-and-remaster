p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\presentation.rs'
t = open(p, encoding='utf8').read()
def rep(a, b, n=1):
    global t
    assert a in t, a[:60]
    t = t.replace(a, b, n)

# light: crisper near shadows, warm sun
rep('''        DirectionalLight {
            illuminance: 3200.,
            shadows_enabled: true,
            ..default()
        },
        Transform::from_xyz(-10., 24., 10.).looking_at(Vec3::ZERO, Vec3::Y),''', '''        DirectionalLight {
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
        Transform::from_xyz(-10., 24., 10.).looking_at(Vec3::ZERO, Vec3::Y),''')
# HDR scene camera (bloom/tonemapping toggled by the effects system)
rep('''        Msaa::Off,
        Tonemapping::None,
        Transform::default(),
    ));''', '''        Msaa::Off,
        Tonemapping::None,
        bevy::render::view::Hdr,
        Transform::default(),
    ));''')
# ledges / white blocks use Let's Go cliff and stone textures
rep('''                if tile == b'L' {
                    block(''', '''                if tile == b'L' && kit {
                    c.spawn((Geometry, Mesh3d(a.cube.clone()), MeshMaterial3d(a.kit_cliff.clone()),
                        Transform::from_translation(p + Vec3::Y * 0.12 + origin()).with_scale(Vec3::new(1., 0.24, 0.8))));
                } else if tile == b'L' {
                    block(''')
rep('''                } else if tile == b'X' && !covered {
                    block(''', '''                } else if tile == b'X' && !covered && kit {
                    c.spawn((Geometry, Mesh3d(a.cube.clone()), MeshMaterial3d(a.kit_stone.clone()),
                        Transform::from_translation(p + Vec3::Y * 0.25 + origin()).with_scale(Vec3::new(0.98, 0.5, 0.98))));
                } else if tile == b'X' && !covered {
                    block(''')
rep('''    kit_flower: Handle<Scene>,''', '''    kit_flower: Handle<Scene>,
    kit_cliff: Handle<StandardMaterial>,
    kit_stone: Handle<StandardMaterial>,''')
rep('''    let kit_path = kit_material("letsgo/kit/soil01_com.png", &mut materials);''', '''    let kit_path = kit_material("letsgo/kit/soil01_com.png", &mut materials);
    let kit_cliff = kit_material("letsgo/kit/cliff01_com.png", &mut materials);
    let kit_stone = kit_material("letsgo/kit/rock01_com.png", &mut materials);''')
rep('''        kit_flower: scene("letsgo/kit/flower.glb"),''', '''        kit_flower: scene("letsgo/kit/flower.glb"),
        kit_cliff,
        kit_stone,''')
# trees a bit smaller, grass/trees sway
rep('''                            piece(c, &a.kit_tree, Vec3::new(x as f32 + 0.5, 0., z as f32 + 0.5), x * 31 + z * 17);''',
    '''                            piece(c, &a.kit_tree, Vec3::new(x as f32 + 0.5, 0., z as f32 + 0.5), x * 31 + z * 17, 0.8, 0.012);''')
rep('''                    piece(c, &a.kit_grass_piece, p, x * 7 + z * 13);''', '''                    piece(c, &a.kit_grass_piece, p, x * 7 + z * 13, 1., 0.07);''')
rep('''                    piece(c, &a.kit_flower, p, x * 11 + z * 5);''', '''                    piece(c, &a.kit_flower, p, x * 11 + z * 5, 1., 0.04);''')
rep('''fn piece(c: &mut Commands, scene: &Handle<Scene>, at: Vec3, seed: usize) {
    // Small deterministic yaw / scale jitter so repeated pieces read as natural.
    let yaw = (seed % 12) as f32 * 0.52;
    let scale = 0.92 + (seed % 5) as f32 * 0.04;
    c.spawn((Geometry, SceneRoot(scene.clone()),
        Transform::from_translation(at + origin()).with_rotation(Quat::from_rotation_y(yaw)).with_scale(Vec3::splat(scale))));
}''', '''fn piece(c: &mut Commands, scene: &Handle<Scene>, at: Vec3, seed: usize, size: f32, sway: f32) {
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
        *t = if p.effects { Tonemapping::AcesFitted } else { Tonemapping::None };
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
}''')
# camera boom also avoids tree canopies outdoors
rep('''                    } else {
                        2.6
                    };''', '''                    } else {
                        3.6
                    };''')
open(p, 'w', encoding='utf8').write(t)

m = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\main.rs'
t = open(m, encoding='utf8').read()
rep('''    letsgo_scenery: bool,
    native_state: bool,''', '''    letsgo_scenery: bool,
    effects: bool,
    native_state: bool,''')
rep('''            letsgo_scenery: !ini.contains("letsgo_scenery=0"),''', '''            letsgo_scenery: !ini.contains("letsgo_scenery=0"),
            effects: !ini.contains("effects=0"),''')
rep('''        let text = format!("{text}letsgo_scenery={}\\n", u8::from(self.letsgo_scenery));''',
    '''        let text = format!("{text}letsgo_scenery={}\\neffects={}\\n", u8::from(self.letsgo_scenery), u8::from(self.effects));''')
rep('''        if keys.just_pressed(KeyCode::KeyL) {''', '''        if keys.just_pressed(KeyCode::KeyK) {
            p.effects = !p.effects;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyL) {''')
rep('''   L: Let's Go scenery: {}",''', '''   L: Let's Go scenery: {}   K: Effects: {}",''')
rep('''            if p.letsgo_scenery { "ON" } else { "OFF" }
        ));''', '''            if p.letsgo_scenery { "ON" } else { "OFF" },
            if p.effects { "ON" } else { "OFF" }
        ));''')
rep('''                presentation::animate,''', '''                presentation::animate,
                presentation::effects,
                presentation::sway,''')
open(m, 'w', encoding='utf8').write(t)
print('ok')
