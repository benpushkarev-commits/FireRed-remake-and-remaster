import re
p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\presentation.rs'
t = open(p, encoding='utf8').read()

# 1. origin offset for every geometry spawn site
t = t.replace('''fn block(c: &mut Commands, a: &Art, p: Vec3, size: Vec3, color: usize) -> Entity {
    c.spawn((
        Geometry,
        Mesh3d(a.cube.clone()),
        MeshMaterial3d(a.colors[color].clone()),
        Transform::from_translation(p).with_scale(size),''', '''thread_local! {
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
        Transform::from_translation(p + origin()).with_scale(size),''', 1)
t = t.replace('''        MeshMaterial3d(material),
        Transform::default(),
    ));
}''', '''        MeshMaterial3d(material),
        Transform::from_translation(origin()),
    ));
}''', 1)
t = t.replace('''            Transform::from_xyz(
                x,
                2.35,
                z - (d + 0.6) / 2. + band_depth * (band as f32 + 0.5),
            ),''', '''            Transform::from_xyz(
                x,
                2.35,
                z - (d + 0.6) / 2. + band_depth * (band as f32 + 0.5),
            )
            .with_translation(origin() + Vec3::new(x, 2.35, z - (d + 0.6) / 2. + band_depth * (band as f32 + 0.5))),''', 1)

# 2. move per-map scenery spawning into its own fn
start = t.index('    if !letsgo {\n    block(')
end_marker = '    }\n    for n in &m.npcs {'
end = t.index(end_marker, start)
body = t[start + len('    if !letsgo {\n'):end]
body = body.replace('&mut c,', 'c,').replace('&mut c, ', 'c, ').replace('&mut meshes', 'meshes')
body = body.replace('(&mut c', '(c')
fn = '''/// FireRed block scenery for one map, offset by the current ORIGIN.
fn spawn_map_blocks(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, m: &firered_native_prototype::Map, preferences: &Preferences) {
    let inside = m.indoor;
    let width = m.tiles[0].len() as f32;
    let depth = m.tiles.len() as f32;
''' + body + '}\n'
old_head = t[t.index('    // Let\'s Go scenery replaces the cosmetic blocks only'):end + len('    }\n')]
new_head = '''    // Outdoor maps stream in with their connected neighbours so walking between them has no
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
        if preferences.letsgo_scenery && !map.indoor && scenery.0.contains_key(id.as_str()) {
            for (i, h) in scenery.0[id.as_str()].iter().enumerate() {
                if !kept.iter().any(|(k, j)| k == id && *j == i) {
                    c.spawn((SceneRoot(h.clone()), Transform::from_translation(*o), LetsGoScene(id.clone(), i)));
                }
            }
        } else {
            ORIGIN.with(|v| v.set(*o));
            spawn_map_blocks(&mut c, &a, &mut meshes, map, &preferences);
            ORIGIN.with(|v| v.set(Vec3::ZERO));
        }
    }
'''
t = t.replace(old_head, new_head, 1)
t = t.replace('pub(super) fn rebuild(', fn + '''#[derive(Component)]
pub(super) struct LetsGoScene(String, usize);
/// (offset of the previous map in the current map's frame, previous map id)
#[derive(Resource, Default)]
pub(super) struct WorldShift(pub Vec3, pub String);
/// Connected outdoor maps around `start` with their offsets in start's tile frame.
pub(super) fn neighbourhood(world: &World, start: &str, depth: usize) -> Vec<(String, Vec3)> {
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
pub(super) fn rebuild(''', 1)
t = t.replace('''    scenery: Res<models::Scenery>,
) {
    if !s.dirty {''', '''    scenery: Res<models::Scenery>,
    mut scenes: Query<(Entity, &LetsGoScene, &mut Transform)>,
    mut shift: ResMut<WorldShift>,
) {
    if !s.dirty {''', 1)
open(p, 'w', encoding='utf8').write(t)
print('ok', 'spawn_map_blocks' in t, 'neighbourhood' in t)
