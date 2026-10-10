# Building, deploying and debugging

All commands run from the project root on Windows.

## One-time setup

`tools\SETUP.bat` installs MSYS2 (gcc, CMake, Ninja, ccache), Git, ripgrep and
Python with winget. MSYS2 must end up in `C:\msys64`.

## Build

`tools\build.bat` → `build\FireRed Remaster.exe`

- Configures once (CMake + Ninja, Release, `-O1`, statically linked runtime),
  then rebuilds only what changed. `tools\build.bat -Clean` starts over.
- Offline: SDL2 2.32.10 and toml++ come from `deps\` (no downloads).
- The first build compiles ~444 MB of generated C and takes a while; ccache
  makes later clean builds faster.
- Target: `FireRedRecomp` only.

## Deploy

`tools\deploy.bat` copies the new exe into the game folder
(`tools\paths.bat` → `%USERPROFILE%\Desktop\FireRedRecomp-esc-menu`), keeping
the existing exe name (`Pokemon Fire Red.exe`) and backing up the old one to
`install_files\old\`. Close the game first.

Only the exe changes between builds; the game folder already has SDL2.dll,
assets, the BIOS, ROM path (`rom.cfg`) and saves.

## Debug

- `tools\run-debug.bat` starts the game with `--tcp-observe 19842`.
- `python tools\py\tcp.py u16 0x03005008` reads guest memory; `raw` sends any
  command from `FireRedLeafGreenRecomp\gbarecomp\TCP.md` (save states:
  `{"cmd":"savestate_save","path":"name.state"}` / `savestate_load`).
- stderr log lines are prefixed `[frlg-hd]`, `[frlg-pc]`, `[frlg:view]`, `[frlg-3d]`.
- `FRLG_3D_ALPHA`, `FRLG_3D_FOV`, `FRLG_3D_ZOOM`, `FRLG_3D_TIMING` env vars tune
  or time the 3D view for testing.

## Icon

`python tools\py\apply_icon.py some.ico` regenerates `res\firered_remaster.ico`
with every Windows size, then rebuild.
