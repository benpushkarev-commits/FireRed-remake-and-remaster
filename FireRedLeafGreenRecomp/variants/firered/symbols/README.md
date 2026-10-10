# symbols/

Imported symbol map for Pokémon FireRed (USA v1.0). Files here are
produced by `tools/import_pokefirered_symbols/`; do not hand-edit unless
explicitly adding a symbol the decomp doesn't have.

| File                          | What it is                                          |
|-------------------------------|-----------------------------------------------------|
| `imported_symbols.tsv`        | Functions — `addr  mode  name`                      |
| `function_boundaries.tsv`     | Functions — `start  end  mode  name` (end exact)    |
| `imported_data_symbols.tsv`   | Data — `addr  region  name`                         |
| `all_symbols.tsv`             | LOSSLESS master — `addr  size  region  kind  mode  name` |
| `firered.toml`                | `gba_recompile` config seed (program + extra_func)  |

Schema:

- `addr`, `start`, `end` are hex with `0x` prefix.
- `mode` is `arm` or `thumb`.
- `name` is the decomp's symbol name verbatim.

## Where the data comes from

`pret/pokefirered` ships prebuilt symbol maps on its `symbols` branch.
We vendor `pokefirered.sym` under `third_party/pokefirered/` and parse
it. The decomp's C source is **never** read — only `(name, addr, size)`.

The `.sym` line format is `addr  binding(g/l/w)  size  name`.

## ⚠️ Classification is heuristic — by necessity

The `.sym` carries **binding and size but no type** (text vs rodata),
and ROM code is non-contiguous (`.text … script_data … lib_text …
.rodata`), so no single address boundary separates code from data. A
byte-accurate partition needs a built `.map` (devkitARM/agbcc — gated
behind the tool worktree we haven't created) or the recompiler's own
discovery pass.

So the importer classifies a ROM symbol as a **function** when
`size > 0` **and** its name doesn't match pret data-naming conventions
(`g*`/`s*` globals, `_Pal`/`_Tiles`/`_Gfx`/… suffixes, `_EventScript_`/
`_MapScripts` script labels). Everything else is **data**. Mode defaults
to THUMB (pokefirered is THUMB bar a tiny ARM entry/IRQ path the
recompiler discovers).

Current seed (regenerate with the importer): **12,675 functions /
38,131 data / 50,806 total**. This is deliberately conservative — a
missed function is cheaply rediscovered; a misclassified data-as-code
symbol makes the recompiler emit garbage. `all_symbols.tsv` keeps every
entry losslessly so nothing is lost to the heuristic.

If a function is missing here, it shows up in `dispatch_misses.log`
after a run and gets added to `game.toml [functions]`. The intended
workflow is to start from this full seed and **back entries out as the
heuristic finder rediscovers them**, measuring the finder by how much of
the decomp map it recovers unaided.
