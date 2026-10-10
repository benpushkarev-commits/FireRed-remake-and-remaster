p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\presentation.rs'
t = open(p, encoding='utf8').read()
pairs = [
('''            let point = target + direction * t;
            let cell = Cell(point.x.round() as i32, point.z.round() as i32);''',
'''            let point = target + direction * t;
            let cell = Cell(point.x.round() as i32, point.z.round() as i32);
            let ground = scenery.ground(p.letsgo_scenery, &s.game.map, point.x, point.z);
            let up = point.y - ground;'''),
('''                    && point.y < 3.9
            });''', '''                    && up < 3.9
            });'''),
('''                && point.y
                    < if m.indoor {''', '''                && up
                    < if m.indoor {'''),
('''            if building || solid {
                distance = (t - 0.25).max(0.25);''', '''            if building || solid || up < 0.35 {
                distance = (t - 0.25).max(0.25);'''),
('''    time: Res<Time>,
    mut rig: Local<(Option<Vec3>, f32, String)>,
    shift: Res<WorldShift>,
) {''', '''    time: Res<Time>,
    mut rig: Local<(Option<Vec3>, f32, String)>,
    shift: Res<WorldShift>,
    scenery: Res<models::Scenery>,
) {'''),
]
for a, b in pairs:
    assert a in t, a[:60]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
