p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\presentation.rs'
t = open(p, encoding='utf8').read()
pairs = [
('''            if building || solid || up < 0.35 {''', '''            if building || solid {'''),
('''        rig.1 = boom;
        *camera.0 =
            Transform::from_translation(target + direction * boom).looking_at(target, Vec3::Y);''',
'''        rig.1 = boom;
        // Ride over Let's Go terrain instead of collapsing into the player on slopes.
        let mut eye = target + direction * boom;
        let floor = scenery.ground(p.letsgo_scenery, &s.game.map, eye.x, eye.z) + 0.6;
        eye.y = eye.y.max(floor);
        *camera.0 = Transform::from_translation(eye).looking_at(target, Vec3::Y);'''),
]
for a, b in pairs:
    assert a in t, a[:60]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
