p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\presentation.rs'
t = open(p, encoding='utf8').read()
a = t.index('fn spawn_map_blocks(')
b = t.index('#[derive(Component)]\npub(super) struct LetsGoScene')
f = t[a:b]
orig = f
f = f.replace('fn spawn_map_blocks(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, m: &firered_native_prototype::Map, preferences: &Preferences) {',
              'fn spawn_map_blocks(c: &mut Commands, a: &Art, meshes: &mut Assets<Mesh>, m: &firered_native_prototype::Map, preferences: &Preferences, kit: &models::Kit) {\n'
              '    // Let\'s Go kit pieces dress maps that have no matching Let\'s Go layout.\n'
              '    let kit = kit.ready && preferences.letsgo_scenery && !m.indoor;')
f = f.replace('    if preferences.source_art {\n        surface(', '    if preferences.source_art && !kit {\n        surface(', 1)
# ground tiles
old_ground = '''            block(
                c,
                &a,
                p + Vec3::new(0., -0.015, 0.),
                Vec3::new(1., 0.04, 1.),
                color,
            );'''
assert old_ground in f, 'ground'
f = f.replace(old_ground, '''            if kit && !inside && tile != b'W' {
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
            }''', 1)
old_tree = '''                    if x % 2 == 0 && z % 2 == 0 {
                        tree(c, &a, x as f32 + 0.5, z as f32 + 0.5);
                    }'''
assert old_tree in f, 'tree'
f = f.replace(old_tree, '''                    if x % 2 == 0 && z % 2 == 0 {
                        if kit {
                            piece(c, &a.kit_tree, Vec3::new(x as f32 + 0.5, 0., z as f32 + 0.5), x * 31 + z * 17);
                        } else {
                            tree(c, &a, x as f32 + 0.5, z as f32 + 0.5);
                        }
                    }''', 1)
old_grass = "                if tile == b'g' {\n"
assert old_grass in f, 'grass'
f = f.replace(old_grass, "                if tile == b'g' && kit {\n                    piece(c, &a.kit_grass_piece, p, x * 7 + z * 13);\n                } else if tile == b'g' {\n", 1)
old_flower = "                if tile == b'f' {\n"
assert old_flower in f, 'flower'
f = f.replace(old_flower, "                if tile == b'f' && kit {\n                    piece(c, &a.kit_flower, p, x * 11 + z * 5);\n                } else if tile == b'f' {\n", 1)
f += '''fn piece(c: &mut Commands, scene: &Handle<Scene>, at: Vec3, seed: usize) {
    // Small deterministic yaw / scale jitter so repeated pieces read as natural.
    let yaw = (seed % 12) as f32 * 0.52;
    let scale = 0.92 + (seed % 5) as f32 * 0.04;
    c.spawn((Geometry, SceneRoot(scene.clone()),
        Transform::from_translation(at + origin()).with_rotation(Quat::from_rotation_y(yaw)).with_scale(Vec3::splat(scale))));
}
'''
t = t[:a] + f + t[b:]
t = t.replace('spawn_map_blocks(&mut c, &a, &mut meshes, map, &preferences);', 'spawn_map_blocks(&mut c, &a, &mut meshes, map, &preferences, &kit);', 1)
t = t.replace('''    mut shift: ResMut<WorldShift>,
) {
    if !s.dirty {''', '''    mut shift: ResMut<WorldShift>,
    kit: Res<models::Kit>,
) {
    if !s.dirty {''', 1)
# Art fields
t = t.replace('''pub(super) struct Art {
    cube: Handle<Mesh>,''', '''pub(super) struct Art {
    cube: Handle<Mesh>,
    kit_grass: Handle<StandardMaterial>,
    kit_path: Handle<StandardMaterial>,
    kit_tree: Handle<Scene>,
    kit_grass_piece: Handle<Scene>,
    kit_flower: Handle<Scene>,''', 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
