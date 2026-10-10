# FireRed Remaster — instructions for AI coding agents

Read by Codex, Claude Code (via CLAUDE.md), Cursor, Gemini CLI and others.
Keep this file short: it is loaded at the start of every session.

## What this is

Ben's personal PC "remaster" of Pokémon FireRed (USA rev0), built on a static
recompilation of the GBA game. The original game logic runs as recompiled C++;
everything added sits on top as presentation and PC features: a modern HD
interface, 16:9 widescreen, keyboard/mouse/controller controls, settings
screens, fast forward, infinite Rare Candy, and an experimental 3D "diorama"
view.

## Rules (from Ben — always follow)

- This repo is PUBLIC. Commits/pushes are authorized, but never commit ROMs, BIOS, saves, generated C, extracted game data, or any Pokemon/Let's Go assets (models, textures, audio, Switch/NCA files, keys).
- New features must be switchable in the game's Settings (Options → System →
  Extras, or the Interface page) and saved in `config.ini`.
- Ben is casual and terse; give short, direct answers.
- Deliver changes by building and deploying (below). Keep the exe name Ben uses
  in his game folder (currently `Pokemon Fire Red.exe`).
- Prefer **Claude Code cloud sessions** for implementation to reduce Codex usage.
  Codex coordinates focused private-branch patches, reviews them, then builds,
  silently checks visuals and deploys locally. Local Claude is not the default.
- Make routine project decisions autonomously; ask only for a real blocker or
  required approval. Keep game audio muted and add no zoom settings.

## Layout

```
FireRedLeafGreenRecomp/        game repo (CMake root)
  src/                         ALL remaster code lives here (fr_*.cpp/h)
  gbarecomp/                   engine (PPU, runtime, host window) — a few small changes
  recomp-ui/                   launcher UI + Dear ImGui
  variants/firered/            game.toml, symbols/, generated/ (DO NOT READ), roms/
  res/                         exe icon + version info, fonts source
deps/                          bundled SDL2 + toml++ for offline builds (don't read)
tools/                         one-command scripts (see below) + tools/py helpers
docs/STATE.md                  features, file map, config keys, gotchas, TODO
docs/BUILD.md                  build / deploy / debug details
```

## Keeping usage low (important)

- Never open `variants/*/generated/` (444 MB of machine-generated C), `deps/`,
  `build/` or `src/fr_hd_fonts.cpp` (embedded font data).
- Search with ripgrep (`rg`), then read only the lines you need.
- Guest memory addresses: `rg <symbol> FireRedLeafGreenRecomp/variants/firered/symbols/all_symbols.tsv`
  (pret/pokefirered symbol names). Shared addresses are in `src/fr_guest.h`.
- Read `docs/STATE.md` before starting; update it when you finish something.
- Images are expensive. Capture a screenshot only to check a visual change,
  open only the new capture, and never browse old ones in `tools/check-game/`.
- Build output and logs: report only errors/warnings that matter, not whole logs.

## Token-saving tools (see docs/TOKEN-SAVERS.md)

- **Graphify**: before searching code, ask the map:
  `graphify query "<question>"` (graph in `graphify-out/`; run
  `graphify update .` after big changes).
- **RTK** rewrites shell commands automatically to trim their output.
- **Ponytail** (Claude plugin) keeps code minimal, but guest-memory validity
  checks and settings toggles are requirements, not over-engineering.

## Commands (Windows, from the project root)

| Task | Command |
|---|---|
| First-time tool install | `tools\SETUP.bat` (Ben runs this once) |
| Build | `tools\build.bat` (`tools\build.bat -Clean` for a fresh build) |
| Copy into Ben's game folder | `tools\deploy.bat` (game must be closed) |
| Run with debug port 19842 | `tools\run-debug.bat` |
| Read game memory while it runs | `python tools\py\tcp.py u16 0x03005008` (see the script) |
| 3D renderer unit checks (no game needed) | `python tools\py\check_diorama.py C:\msys64\mingw64\bin\c++.exe` |
| Silent screenshot of the game (3D + HD) | `powershell -File tools\capture-3d.ps1 -Name <name>` → `tools\check-game\<name>.bmp` (copy the fresh `build\FireRed Remaster.exe` to `tools\check-game\FireRedCheck.exe` first; options in docs/STATE.md) |

Build target is `FireRedRecomp` only (the LeafGreen target does not link).
Generated code is never edited by hand.

## Code conventions

- C++17, 4-space indent, comments explain *why*. Presentation only: read guest
  memory via `Mem` (`src/fr_guest.h`); never write guest state except where a
  feature needs it (Rare Candy uses `bus_write_*`).
- HD UI drawing goes through `src/fr_hd_draw.h` (view units: 160 tall).
- UTF-8 in C strings as escapes (`"\xc2\xb7"`), not raw characters. The UI
  font has no "›"; use "»".

