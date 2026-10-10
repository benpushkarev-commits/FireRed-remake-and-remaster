p = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\models.rs'
t = open(p, encoding='utf8').read()
pairs = [
# Character state for battle reactions
('''    previous: Option<Vec3>,
    speed: f32,
    motion: usize,
}''', '''    previous: Option<Vec3>,
    speed: f32,
    motion: usize,
    /// Battle reactions: HP seen last frame for (self, opponent) and the one-shot clip playing.
    last_hp: Option<(u16, u16)>,
    action: Option<(usize, f32)>,
}'''),
('''        Character { key: key.into(), ready: false, role, previous: None, speed: 0., motion: 0 },''',
 '''        Character { key: key.into(), ready: false, role, previous: None, speed: 0., motion: 0, last_hp: None, action: None },'''),
('''#[derive(Component)]
pub(super) struct Playback {
    owner: Entity,
    clips: [AnimationNodeIndex; 3],
    current: usize,
}''', '''#[derive(Component)]
pub(super) struct Playback {
    owner: Entity,
    /// Idle, Walk, Run, then optional Attack, Hurt, Faint (missing ones fall back to Idle).
    clips: [AnimationNodeIndex; 6],
    current: usize,
}
const MOTIONS: [&str; 6] = ["Idle", "Walk", "Run", "Attack", "Hurt", "Faint"];'''),
('''            let Some(clips) = ["Idle", "Walk", "Run"].into_iter().map(|name| gltf.named_animations.get(name).cloned()).collect::<Option<Vec<_>>>() else { continue };
            let (graph, indices) = AnimationGraph::from_clips(clips);
            let mut transitions = AnimationTransitions::new();
            transitions.play(&mut player, indices[0], Duration::ZERO).repeat();
            c.entity(child).insert((AnimationGraphHandle(graphs.add(graph)), transitions,
                Playback { owner: event.entity, clips: [indices[0], indices[1], indices[2]], current: 0 }));''',
 '''            let Some(base) = ["Idle", "Walk", "Run"].into_iter().map(|name| gltf.named_animations.get(name).cloned()).collect::<Option<Vec<_>>>() else { continue };
            let extra: Vec<_> = ["Attack", "Hurt", "Faint"].into_iter()
                .map(|name| gltf.named_animations.get(name).cloned().unwrap_or_else(|| base[0].clone())).collect();
            let (graph, indices) = AnimationGraph::from_clips(base.into_iter().chain(extra));
            let mut transitions = AnimationTransitions::new();
            transitions.play(&mut player, indices[0], Duration::ZERO).repeat();
            c.entity(child).insert((AnimationGraphHandle(graphs.add(graph)), transitions,
                Playback { owner: event.entity, clips: std::array::from_fn(|i| indices[i]), current: 0 }));'''),
# battle sway only for models without source clips; reactions from HP changes
('''                previews[i] = true;
''', '''                previews[i] = true;
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
'''),
('''        root.motion = if !enabled || !p.character_animation || !moving { 0 }
            else if root.speed > run_from || grid_run { 2 } else { 1 };''', '''        root.motion = if !enabled || !p.character_animation || !moving { 0 }
            else if root.speed > run_from || grid_run { 2 } else { 1 };
        if let Some((clip, until)) = root.action {
            if time.elapsed_secs() < until && p.character_animation { root.motion = clip; } else { root.action = None; }
        }'''),
('''            transitions.play(&mut player, playback.clips[root.motion], duration).repeat();
            playback.current = root.motion;
            eprintln!("[slice-model] {} {}", root.key, ["Idle", "Walk", "Run"][root.motion]);''',
 '''            let active = transitions.play(&mut player, playback.clips[root.motion], duration);
            if root.motion < 3 { active.repeat(); }
            playback.current = root.motion;
            eprintln!("[slice-model] {} {}", root.key, MOTIONS[root.motion]);'''),
('''        let native = catalog.0.get(&root.key).map_or(0., |m| m.speeds[playback.current]);''',
 '''        let native = if playback.current < 3 { catalog.0.get(&root.key).map_or(0., |m| m.speeds[playback.current]) } else { 0. };'''),
]
for a, b in pairs:
    assert a in t, a[:70]
    t = t.replace(a, b, 1)
open(p, 'w', encoding='utf8').write(t)
print('ok')
