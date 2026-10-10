import re
root = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src'
def edit(fn, pairs):
    p = root + '\\' + fn
    t = open(p, encoding='utf8').read()
    for a, b in pairs:
        assert a in t, (fn, a[:80])
        t = t.replace(a, b, 1)
    open(p, 'w', encoding='utf8').write(t)

edit('models.rs', [
('''#[derive(Resource, Default)]
pub(super) struct Scenery(pub HashMap<String, Vec<Handle<Scene>>>);''',
'''#[derive(Resource, Default)]
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
}'''),
('''    let mut out = HashMap::new();
    for (map, entries) in manifest.as_object().into_iter().flatten() {
        let scenes = entries.as_array().into_iter().flatten()
            .filter_map(|e| e["path"].as_str())
            .map(|p| server.load(GltfAssetLabel::Scene(0).from_asset(p.to_owned())))
            .collect();
        out.insert(map.clone(), scenes);
    }
    Scenery(out)''',
'''    let root = path.parent().and_then(|p| p.parent()).map(|p| p.to_path_buf()).unwrap_or_default();
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
    out'''),
('''                    if let Some((at, facing)) = field.actors.displayed(id) {
                        transform.translation = Vec3::new(at[0], 0.025, at[1]);''',
'''                    if let Some((at, facing)) = field.actors.displayed(id) {
                        transform.translation = Vec3::new(at[0], 0.025 + scenery.ground(p.letsgo_scenery, &s.game.map, at[0], at[1]), at[1]);'''),
('''    shift: Res<presentation::WorldShift>,
) {''', '''    shift: Res<presentation::WorldShift>,
    scenery: Res<Scenery>,
) {'''),
])

edit('presentation.rs', [
('scenery.0.contains_key(id.as_str())', 'scenery.scenes.contains_key(id.as_str())'),
('for (i, h) in scenery.0[id.as_str()].iter().enumerate()', 'for (i, h) in scenery.scenes[id.as_str()].iter().enumerate()'),
# overlay maps get both the Let's Go scene and kit dressing
('''        if preferences.letsgo_scenery && !map.indoor && scenery.scenes.contains_key(id.as_str()) {''',
 '''        let lg = preferences.letsgo_scenery && !map.indoor && scenery.scenes.contains_key(id.as_str());
        if lg {'''),
('''        } else {
            ORIGIN.with(|v| v.set(*o));
            spawn_map_blocks(&mut c, &a, &mut meshes, map, &preferences, &kit);
            ORIGIN.with(|v| v.set(Vec3::ZERO));
        }''', '''        }
        if !lg || scenery.overlay.contains(id.as_str()) {
            ORIGIN.with(|v| v.set(*o));
            let ground = if lg { scenery.heights.get(id.as_str()) } else { None };
            spawn_map_blocks(&mut c, &a, &mut meshes, map, &preferences, &kit, ground);
            ORIGIN.with(|v| v.set(Vec3::ZERO));
        }'''),
('fn spawn_map_blocks(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, m: &firered_native_prototype::Map, preferences: &Preferences, kit: &models::Kit) {',
 '''fn spawn_map_blocks(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, m: &firered_native_prototype::Map, preferences: &Preferences, kit: &models::Kit, ground: Option<&models::Heights>) {
    // On Let's Go terrain (overlay maps) the ground itself comes from the Let's Go scene;
    // only FireRed features are dressed, standing on the sampled terrain height.
    let terrain = ground.is_some();
    let lift = |x: f32, z: f32| ground.map_or(0., |g| g.at(x, z));
    let lg_tree_near = |x: f32, z: f32| ground.is_some_and(|g| g.trees.iter().any(|t| t.distance(Vec2::new(x, z)) < 1.6));'''),
])

# inside spawn_map_blocks: skip base / ground tiles on terrain, lift pieces
p = root + '\\presentation.rs'
t = open(p, encoding='utf8').read()
a = t.index('fn spawn_map_blocks(')
b = t.index('fn piece(c: &mut Commands')
f = t[a:b]
def r(x, y):
    global f
    assert x in f, x[:80]
    f = f.replace(x, y, 1)
r('''    block(
        c,
        &a,
        Vec3::new((width - 1.) / 2., -0.26, (depth - 1.) / 2.),''', '''    if !terrain {
    block(
        c,
        &a,
        Vec3::new((width - 1.) / 2., -0.26, (depth - 1.) / 2.),''')
r('''        if inside { BARK } else { GRASS },
    );''', '''        if inside { BARK } else { GRASS },
    );
    }''')
r('''            if kit && !inside && tile != b'W' {''', '''            if terrain {
                // Let's Go terrain already covers the ground
            } else if kit && !inside && tile != b'W' {''')
r('''                        if kit {
                            piece(c, &a.kit_tree, Vec3::new(x as f32 + 0.5, 0., z as f32 + 0.5), x * 31 + z * 17, 0.8, 0.012);''',
  '''                        let (tx, tz) = (x as f32 + 0.5, z as f32 + 0.5);
                        if terrain && lg_tree_near(tx, tz) {
                            // a Let's Go tree already stands here
                        } else if kit {
                            piece(c, &a.kit_tree, Vec3::new(tx, lift(tx, tz), tz), x * 31 + z * 17, 0.8, 0.012);''')
r('''Transform::from_translation(p + Vec3::Y * 0.25 + origin()).with_scale(Vec3::new(0.98, 0.5, 0.98))));''',
  '''Transform::from_translation(p + Vec3::Y * (0.25 + lift(p.x, p.z)) + origin()).with_scale(Vec3::new(0.98, 0.5, 0.98))));''')
r('''Transform::from_translation(p + Vec3::Y * 0.12 + origin()).with_scale(Vec3::new(1., 0.24, 0.8))));''',
  '''Transform::from_translation(p + Vec3::Y * (0.12 + lift(p.x, p.z)) + origin()).with_scale(Vec3::new(1., 0.24, 0.8))));''')
r('''                    piece(c, &a.kit_grass_piece, p, x * 7 + z * 13, 1., 0.07);''',
  '''                    piece(c, &a.kit_grass_piece, p + Vec3::Y * lift(p.x, p.z), x * 7 + z * 13, 1., 0.07);''')
r('''                    piece(c, &a.kit_flower, p, x * 11 + z * 5, 1., 0.04);''',
  '''                    piece(c, &a.kit_flower, p + Vec3::Y * lift(p.x, p.z), x * 11 + z * 5, 1., 0.04);''')
t = t[:a] + f + t[b:]
open(p, 'w', encoding='utf8').write(t)

# player standing height: advance() in main
edit('main.rs', [
('''    if s.free_active {
        s.displayed = Vec3::new(s.free.position[0], s.free.height, s.free.position[1]);
        s.moving = s.free.moving;
        return;
    }''', '''    if s.free_active {
        let ground = scenery.ground(p.letsgo_scenery, &s.game.map, s.free.position[0], s.free.position[1]);
        s.displayed = Vec3::new(s.free.position[0], s.free.height + ground, s.free.position[1]);
        s.moving = s.free.moving;
        return;
    }'''),
('''            if m.jump {
                displayed.y = (t * std::f32::consts::PI).sin() * 0.6;
            }
            s.displayed = displayed;''', '''            if m.jump {
                displayed.y = (t * std::f32::consts::PI).sin() * 0.6;
            }
            displayed.y += scenery.ground(p.letsgo_scenery, &s.game.map, displayed.x, displayed.z);
            s.displayed = displayed;'''),
('''    battle: Res<battle_screen::Screen>,
) {
    if p.menu || battle.active {
        return;
    }''', '''    battle: Res<battle_screen::Screen>,
    scenery: Res<models::Scenery>,
) {
    if p.menu || battle.active {
        return;
    }'''),
])
print('ok')
