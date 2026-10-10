//! Windows PCM playback only. The offline renderer is not linked into gameplay.
use super::*;
#[link(name = "winmm")]
unsafe extern "system" {
    fn PlaySoundW(sound: *const u16, module: *mut std::ffi::c_void, flags: u32) -> i32;
}
#[derive(Resource)]
pub(super) struct Music {
    pub silent: bool,
    pub base: PathBuf,
    current: Option<String>,
}
impl Music {
    pub fn new(base: PathBuf, silent: bool) -> Self {
        Self {
            silent,
            base,
            current: None,
        }
    }
    pub fn stop(&mut self) {
        unsafe {
            PlaySoundW(std::ptr::null(), std::ptr::null_mut(), 0);
        }
        self.current = None;
    }
}
impl Drop for Music {
    fn drop(&mut self) {
        self.stop();
    }
}
pub(super) fn update(
    s: Res<Simulation>,
    p: Res<Preferences>,
    mut music: ResMut<Music>,
    window: Single<&Window, With<PrimaryWindow>>,
    battle: Res<super::battle_screen::Screen>,
) {
    if music.silent {
        return;
    }
    if !p.music || !window.focused || battle.active {
        if music.current.is_some() {
            music.stop();
        }
        return;
    }
    let theme = if s.game.map == "route1" {
        "route1"
    } else {
        "pallet"
    };
    if music.current.as_deref() == Some(theme) {
        return;
    }
    let path = music.base.join("music").join(format!("{theme}.wav"));
    use std::os::windows::ffi::OsStrExt;
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
    // ASYNC | NODEFAULT | LOOP | FILENAME: playback never blocks the simulation.
    let ok = unsafe { PlaySoundW(wide.as_ptr(), std::ptr::null_mut(), 0x0002000b) };
    if ok != 0 {
        music.current = Some(theme.into());
    } else {
        eprintln!("Cannot play music file {}", path.display());
        music.current = Some(theme.into());
    }
}
