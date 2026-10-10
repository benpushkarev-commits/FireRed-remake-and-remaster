m = r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\src\main.rs'
t = open(m, encoding='utf8').read()
def rep(a, b):
    global t
    assert a in t, a[:70]
    t = t.replace(a, b, 1)
rep('mod presentation;', 'mod presentation;\nmod slice_start;')
rep('''    effects: bool,
    native_state: bool,''', '''    effects: bool,
    quick_start: bool,
    native_state: bool,''')
rep('''            effects: !ini.contains("effects=0"),''', '''            effects: !ini.contains("effects=0"),
            quick_start: !ini.contains("quick_start=0"),''')
rep('''neffects={}\\n", u8::from(self.letsgo_scenery), u8::from(self.effects));''',
    '''neffects={}\\nquick_start={}\\n", u8::from(self.letsgo_scenery), u8::from(self.effects), u8::from(self.quick_start));''')
rep('''        if keys.just_pressed(KeyCode::KeyK) {''', '''        if keys.just_pressed(KeyCode::KeyY) {
            p.quick_start = !p.quick_start;
            p.save();
        }
        if keys.just_pressed(KeyCode::KeyK) {''')
rep('''   K: Effects: {}",''', '''   K: Effects: {}\\nY: Slice quick start (pick a starter, skip story): {}",''')
rep('''            if p.effects { "ON" } else { "OFF" }
        ));''', '''            if p.effects { "ON" } else { "OFF" },
            if p.quick_start { "ON" } else { "OFF" }
        ));''')
rep('''                input,
''', '''                input,
                slice_start::update,
''')
# HUD prompt
rep('''    } else if let Some((name, lines, i)) = &s.game.dialogue {''', '''    } else if slice_start::needs_starter(&p, &session) && s.game.dialogue.is_none() {
        format!(
            "{}   |   PLAYABLE SLICE\\nChoose your partner:   1  Bulbasaur     2  Charmander     3  Squirtle\\nThen explore Pallet Town and Route 1: tall grass has wild Pokemon. Tab: settings",
            s.world.map(&s.game.map).name
        )
    } else if let Some((name, lines, i)) = &s.game.dialogue {''')
open(m, 'w', encoding='utf8').write(t)
print('ok')
