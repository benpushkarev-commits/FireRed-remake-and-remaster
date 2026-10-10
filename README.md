# FireRed Remake and Remaster

A personal, non-commercial fan project with three parts in one repo:

| Folder | What it is |
|---|---|
| `FireRedLeafGreenRecomp/` | **The remaster.** Pokémon FireRed running natively on PC via static recompilation ([gbarecomp](https://github.com/mstan/gbarecomp)), with an HD interface, 16:9 widescreen, keyboard/mouse controls, settings screens and an experimental 3D "diorama" overworld. All remaster code is in `src/fr_*`. |
| `rust-prototype/` | **The remake.** A native Rust rewrite: Pallet Town → Route 1 slice in full 3D with a third-person camera, FireRed's original game logic (battles, encounters, RNG, saves) ported from the [pret/pokefirered](https://github.com/pret/pokefirered) decomp. |
| `letsgo-pipeline/` + `rust-prototype/tools/letsgo/` | **The Let's Go asset pipeline.** Python tools that read models, textures and animations from *your own* Pokémon: Let's Go dump (GFBMDL / GFBANM / GFPAK / BNTX) and convert them to glTF for the Rust remake. |

Also: `tools/` (one-command build/deploy scripts), `docs/` (design notes and `STATE.md` progress log), `deps/` (bundled SDL2 + toml++).

## No game content is included

This repo is **code only**. It contains no ROMs, no BIOS, no saves, no recompiled game code, no data extracted from the game, and no Pokémon or Let's Go assets (models, textures, music, Switch files or keys). You need your own legally obtained copies:

- **Remaster:** your own FireRed (USA) ROM (SHA-1 `41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc`) and GBA BIOS, then regenerate code locally (see `docs/BUILD.md`).
- **Remake:** regenerate `rust-prototype/data/*.json` with the `rust-prototype/tools/import_*.py` scripts (sources listed in the `*-provenance.json` files) and build `rust-prototype/assets/` from your own Let's Go dump with the Let's Go pipeline.
- **Launcher art:** the recomp-ui and box art images were left out; copy `assets/` from [mstan/recomp-ui](https://github.com/mstan/recomp-ui) if the launcher build asks for them.

## Build

- Remaster: `tools\SETUP.bat` once, then `tools\build.bat`.
- Remake: `rust-prototype\build.bat`, then `rust-prototype\run.bat`.

## Licences

`FireRedLeafGreenRecomp/`, `gbarecomp/` and `recomp-ui/` keep their upstream licence (PolyForm Noncommercial 1.0.0, see their `LICENSE` files). `deps/` keep their own licences. Pokémon is © Nintendo / Game Freak / The Pokémon Company; this project is not affiliated with or endorsed by them.
