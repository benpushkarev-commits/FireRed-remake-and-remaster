# Base ROM (and BIOS)

We do **not** ship the ROM or the BIOS. You must provide your own dumps
of both:

1. **BIOS** — shared across every gbarecomp game. Expected at
   `../gbarecomp/bios/gba_bios.bin`, SHA-1
   `300c20df6731a33952ded8c436f7f186d25d3492`.
2. **Cartridge ROM** — your own dump of a known-good FireRed cartridge.
   See below.

The runner refuses to launch unless **both** verify. The BIOS is not
optional: every game in `gbarecomp` boots through the real BIOS
(see `../gbarecomp/PRINCIPLES.md` "BIOS is sacred").

## How to point the build at it

Edit `game.toml` `[rom] path` (or pass `-DFIRERED_ROM=...` at configure
time). The runner verifies the SHA-1 on startup against the table below
and refuses to launch with an unrecognized hash.

## Known-good ROMs

Hashes are cross-verified against `pret/pokefirered`'s own `*.sha1`
build-target files (the decomp's ground truth).

| Region / Rev      | SHA-1                                      | Game code | Internal name      |
|-------------------|--------------------------------------------|-----------|--------------------|
| **USA v1.0** ←ours| `41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc` | `BPRE`    | POKEMON FIRE       |
| USA v1.1          | `dd5945db9b930750cb39d00c84da8571feebf417` | `BPRE`    | POKEMON FIRE       |

For reference, the sibling LeafGreen dumps (a future second target):

| Region / Rev   | SHA-1                                      | Game code |
|----------------|--------------------------------------------|-----------|
| LeafGreen v1.0 | `574fa542ffebb14be69902d1d36f1ec0a4afd71e` | `BPGE`    |
| LeafGreen v1.1 | `7862c67bdecbe21d1d69ce082ce34327e1c6ed5e` | `BPGE`    |

This project currently targets **USA v1.0** (matches `firered.sha1`,
the decomp's canonical build).

## What we don't accept

- **"Cleaned" / re-padded dumps.** During setup a file named
  `Pkmn FireRed v1.0 (Cleaned).gba` was offered; its SHA-1 was
  `c40338de117f8d01fa9d63e8db12844ddff006ef` — matching neither v1.0 nor
  v1.1. It was **rejected** and never copied in. The original cartridge
  image is what hardware sees, including pad bytes; recompiled C is keyed
  to exact bytes at exact addresses.
- Trimmed ROMs (header pad removed).
- IPS/UPS-patched ROMs (translations, randomizers, etc.). A patched ROM
  is a different game and needs its own hash entry.
- Decomp-built ROMs. The decomp produces a byte-different artifact; even
  if it boots, it isn't the original cartridge.
