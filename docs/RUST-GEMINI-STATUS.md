# Rust Remaster — Gemini Status

## Current Batch: Fuchsia City, Gym, Safari Zone Areas & Secret House Geography + Per-Cell encounter_types

### Status
- **State**: Generated data stable, ROM-verified, ready for Codex handoff / compilation.
- **Assigned Scope**:
  1. Expand world geography for Fuchsia City and the entire Safari Zone:
     - Fuchsia City urban area: `FuchsiaCity`, `FuchsiaCity_Gym` (Koga), `FuchsiaCity_House1`, `FuchsiaCity_House2` (Move Deleter), `FuchsiaCity_House3` (Good Rod), `FuchsiaCity_Mart`, `FuchsiaCity_PokemonCenter_1F`.
     - Safari Zone facilities: `FuchsiaCity_SafariZone_Entrance`, `FuchsiaCity_SafariZone_Office`, `FuchsiaCity_WardensHouse` (Gold Teeth / Strength HM).
     - Safari Zone wildlife areas: `SafariZone_Center`, `SafariZone_Center_RestHouse`, `SafariZone_East`, `SafariZone_East_RestHouse`, `SafariZone_North`, `SafariZone_North_RestHouse`, `SafariZone_West`, `SafariZone_West_RestHouse`.
     - Key item destination: `SafariZone_SecretHouse` (HM03 Surf reward).
  2. Maintain per-cell `encounter_types` (bits 24..26: 0=none, 1=land, 2=water) extracted from metatile attributes, ROM-verified across all imported cells against rev0 ROM. (**COMPLETED & VERIFIED**)
  3. Add secondary tilesets `gTileset_FuchsiaGym` (`secondary/fuchsia_gym`) and `gTileset_SafariZoneBuilding` (`secondary/safari_zone_building`), baking new source textures. (**COMPLETED & VERIFIED**)
- **File Ownership & Collaboration Rules Strictly Followed**:
  - **Codex-owned files untouched**: `src/lib.rs`, `src/main.rs`, `src/field_screen.rs`, `src/encounter.rs`, `src/battle.rs`, and `src/core.rs` were **completely avoided and untouched**.
  - **No concurrent building/packaging**: As instructed, did **not** run `cargo`, `tools/build.ps1`, or release packaging. Codex will compile and package following stable handoff.
  - **Installed C++ game untouched**: `Pokemon Fire Red.exe` and `FireRedLeafGreenRecomp/` untouched.
  - **Audio muted**: Audio settings remain muted.

---

### Files Modified & Generated
- `rust-prototype/tools/import_world.py`:
  - Added 19 new maps across Fuchsia City and Safari Zone.
  - Added secondary tilesets `gTileset_FuchsiaGym` and `gTileset_SafariZoneBuilding`.
  - Configured verified walkable spawn coordinates and human-readable names for all 19 maps.
  - Total imported & ROM-verified maps increased from 161 to **180 maps** (now **103,723 original cells** byte-for-byte verified against USA rev0 ROM).
- `rust-prototype/tools/import_art.py`:
  - Added `gTileset_FuchsiaGym` and `gTileset_SafariZoneBuilding` to tileset definitions.
  - Baked textures increased from 167 to **186 source textures**, verified against original ROM palettes and metatile definitions.
- `rust-prototype/tools/import_actors.py`:
  - Imported map objects and coordinate triggers: increased from 680 objects / 99 triggers to **751 objects and 102 coordinate triggers**.
- `rust-prototype/tools/import_field.py`:
  - Reassembled field script bytecode covering all new maps (128,252 bytes, 9,731 labels, 4,580 texts, 213 command handlers).
- `rust-prototype/data/world.json`, `data/actors.json`, `data/field.json`, `data/provenance.json`, `data/art-provenance.json`:
  - Generated and verified from pinned checkout `037335f4c725d7c9aecdac87066f2002b4bd7e14`.
  - Validated 100% against all `World::validate()` constraints in `lib.rs` (all 180 maps checked for unique IDs, ASCII tiles, dimensions, matching cells/behaviors/encounter_types, valid non-blocked spawn coordinates with elevation 0, in-bounds NPCs, valid warp destinations, and directional connections).

---

### Source References
- Metatile encounter attributes: `reference/pokefirered/include/global.fieldmap.h`, `src/fieldmap.c` (`METATILE_ATTRIBUTE_ENCOUNTER_TYPE = 24`, mask `0x07000000`).
- Tileset headers: `reference/pokefirered/src/data/tilesets/headers.h` (`gTileset_FuchsiaGym`, `gTileset_SafariZoneBuilding`).
- Map layouts, scripts and events: `reference/pokefirered/data/maps/FuchsiaCity*/`, `SafariZone_*/`.
- Fuchsia Gym & Koga badge scripts: `reference/pokefirered/data/maps/FuchsiaCity_Gym/scripts.inc`.
- Safari Zone game & Surf secret house: `reference/pokefirered/data/maps/SafariZone_SecretHouse/scripts.inc`.
- Warden Gold Teeth & Strength HM: `reference/pokefirered/data/maps/FuchsiaCity_WardensHouse/scripts.inc`.
- Move Deleter & Good Rod: `reference/pokefirered/data/maps/FuchsiaCity_House2/scripts.inc`, `FuchsiaCity_House3/scripts.inc`.

---

### Verification & Checks Run
1. `python tools/import_world.py`:
   - Output: `Imported and ROM-verified 180 maps; 103723 original cells` (exit code 0).
   - Validated: 180 maps, 103,723 cells, 103,723 behaviors, and 103,723 encounter_types matched ROM byte-for-byte.
2. `python tools/import_art.py`:
   - Output: `Baked 186 source textures; ROM-verified tiles, palettes and metatile definitions` (exit code 0).
3. `python tools/import_actors.py`:
   - Output: `Imported 751 objects and 102 coordinate triggers` (exit code 0).
4. `python tools/import_field.py`:
   - Output: `Imported 128252 bytes, 9731 labels, 4580 texts, 213 command handlers` (exit code 0).
5. Rust `World::validate()` parity check on `data/world.json`:
   - Output: All 180 maps pass 100% of checks (dimensions, ASCII tile representations, cells/behaviors/encounter_types length match, all encounter_types in {0, 1, 2}, spawn points within bounds and non-blocked `(cell >> 10) & 3 == 0`, all warp sources and destinations valid, all map connections valid).

---

### Separation of Geography vs. Missing Story/Trainer Mechanics

#### A. Geography & Navigation Status (COMPLETE)
- All 180 maps have valid walkable spawn cells, verified warps, and correct collision/elevation barriers.
- Fully connected indoor/outdoor geography covering:
  - Fuchsia City urban hub (48x40 grid, connected to Route 15 East and Route 18 West).
  - Fuchsia Gym (15x23 grid, invisible wall maze layout).
  - Fuchsia City Houses 1, 2 (Move Deleter), and 3 (Good Rod).
  - Poké Mart and Pokémon Center 1F.
  - Safari Zone Entrance lobby, Office, and Warden's House.
  - Safari Zone Center, East, North, and West outdoor wilderness zones.
  - Safari Zone rest houses (Center, East, North, West).
  - Safari Zone Secret House (HM03 Surf prize location).

#### B. Missing Story & Trainer Mechanics (DOCUMENTED GAPS)
1. **Fuchsia Gym & Koga / Soul Badge**:
   - Invisible wall maze: special transparent collision metatiles.
   - Koga Gym Leader battle (Koffing Lv37, Muk Lv39, Koffing Lv37, Weezing Lv44), Soul Badge grant (`FLAG_BADGE05_GET`), TM06 Toxic reward, and field Surf permission not yet wired to native battle engine.
2. **Safari Zone Game Rules**:
   - Safari Zone entry fee (¥500), 30 Safari Balls allocation, and 600-step countdown timer before automatic warp-out.
   - Safari battle interface (Throw Ball, Throw Bait, Throw Rock, Run) with dedicated catch factor / flee rate formulas.
3. **Safari Zone Key Item Quests**:
   - Secret House in Safari Zone West: grants HM03 Surf (`ITEM_HM03` / `FLAG_GOT_HM03`).
   - Gold Teeth (`ITEM_GOLD_TEETH`) ground pickup in Safari Zone West.
   - Returning Gold Teeth to Warden gives HM04 Strength (`ITEM_HM04` / `FLAG_GOT_HM04`).
4. **Move Deleter & Good Rod NPCs**:
   - Move Deleter in House 2: unlearning HM and regular moves.
   - Fishing Guru's brother in House 3: Good Rod grant (`ITEM_GOOD_ROD` / `FLAG_GOT_GOOD_ROD`).

---

### Ownership Handoff & Build Coordination
- **Generated Data Stability**: All 180 maps, 186 baked textures, 751 actor objects, 102 triggers, and per-cell `encounter_types` are stable and ROM-verified.
- **Build Lock**: Gemini did **not** invoke `cargo` or `tools/build.ps1`.
- **Handoff Ready**: Codex may proceed to compile, integrate host encounter triggers with `encounter_types`, and package the release.
