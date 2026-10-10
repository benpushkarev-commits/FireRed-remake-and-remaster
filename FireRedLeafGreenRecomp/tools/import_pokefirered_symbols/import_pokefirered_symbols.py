#!/usr/bin/env python3
"""import_pokefirered_symbols.py — read pret/pokefirered's prebuilt
.sym map and emit symbol seeds for gba_recompile.

Unlike zeldaret/tmc (Minish Cap), pokefirered is ~100% decompiled, so
almost no addresses live in `asm/**/*.s` (the directory is empty of
hand-addressed functions). Instead the decomp's `symbols` branch ships
prebuilt symbol maps that we vendor under:

    third_party/pokefirered/<variant>.sym

`.sym` line format (objdump-symtab style, the type column stripped):

    <addr:8hex> <binding:g|l|w> <size:8hex> <name>
    e.g.  08071d64 g 00000018 FadeOutBGMTemporarily   <- function
          02020634 l 00000004 sWindowPtr              <- EWRAM data
          08160479 g 00000000 TradeCenter_MapScripts  <- ROM script data

The decomp's C source is **never** read. Only symbol metadata
(name, address, size) enters this project — same policy as
tools/import_tmc_symbols.

──────────────────────────────────────────────────────────────────────
CLASSIFICATION IS HEURISTIC — and deliberately so. Read this.
──────────────────────────────────────────────────────────────────────
The `.sym` carries *binding* (g/l/w) and *size*, but NO *type*
(text vs rodata). Worse, ROM code is non-contiguous: the linker lays
out  .text … script_data … lib_text … .rodata …  so a single
address boundary cannot separate code from data.

A byte-accurate text/data partition therefore requires a built `.map`
(which needs the devkitARM/agbcc toolchain) OR the recompiler's own
discovery pass. Both are downstream of the `gbarecomp` worktree we
have deliberately NOT created yet (see README.md "Worktree prerequisite").

So here we classify functions vs data by (size > 0) plus pret naming
conventions, default ARM/THUMB to THUMB (pokefirered is thumb
throughout bar a tiny ARM entry path), and — crucially — ALSO emit a
complete, lossless master TSV (all_symbols.tsv) so nothing is thrown
away. When the recompiler is wired up, its dispatch_misses.log loop and
(optionally) a built .map become the authoritative typing; the
heuristic seed here just gives it a strong head start. This matches the
project's "decomp is a reference, not an oracle" rule.

Outputs (mirrors tools/import_tmc_symbols layout):

  symbols/imported_symbols.tsv       functions: addr  mode  name
  symbols/function_boundaries.tsv    functions: start end  mode  name
  symbols/imported_data_symbols.tsv  data:      addr  region name
  symbols/all_symbols.tsv            EVERYTHING: addr size region kind mode name
  symbols/firered.toml               gba_recompile config seed
  ghidra/import_symbols.py           Ghidra Jython script

Usage:
  python tools/import_pokefirered_symbols/import_pokefirered_symbols.py
      [--sym third_party/pokefirered/pokefirered.sym]
      [--rom roms/firered_usa.gba]
      [--out symbols] [--ghidra ghidra] [--toml symbols/firered.toml]
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import sys
from collections import defaultdict
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[2]


@dataclass
class Sym:
    addr: int          # bit-0 stripped (real address)
    size: int
    binding: str       # g | l | w
    name: str
    region: str        # ewram / iwram / rom / ...
    kind: str          # "func" or "data"
    mode: str          # "arm" / "thumb" (meaningful only for kind=func)


# ── .sym line: addr  binding  size  name ─────────────────────────────
SYM_LINE = re.compile(
    r"^([0-9A-Fa-f]{8})\s+([glw])\s+([0-9A-Fa-f]{8})\s+(\S.*?)\s*$"
)


# ── pret naming conventions that mark read-only DATA in ROM ──────────
# Conservative on purpose: a false "data" only costs the recompiler a
# rediscovery (cheap, safe); a false "func" makes it try to decode data
# as THUMB and emit garbage (expensive, unsafe). So when in doubt we
# lean data.
DATA_SUFFIX = re.compile(
    r"_(Pal|Palette|Tiles|Tilemap|Map|Gfx|Sheet|Anim|AnimTable|AnimCmds?|"
    r"AnimTables|Frames?|Coords?|Table|Tables|Ptrs?|Pointers?|Data|Buffer|"
    r"List|Lists|Template|Templates|Layout|Layouts|OamData|Bytes|Strings?|"
    r"Texts?|Graphics|Tilesets?|Palettes)$"
)
DATA_PREFIX = re.compile(
    r"^(g[A-Z0-9]|s[A-Z0-9]|gUnknown_|sUnknown_|Unknown_|gText_|sText_|"
    r"gGraphics_|gObjectEvent|gFieldEffect|gMonFrontPic|gMonBackPic|"
    r"gMonStillFrontPic|gMonPalette|Cry_|BattleScript_|gBattleScript)"
)
SCRIPT_LABEL = re.compile(
    r"_(EventScript|MapScripts?|MapScript[12]|Movement|OnTransition|"
    r"OnResume|OnLoad|OnFrame|OnWarp|OnReturn|OnDiveWarp|EventObjects|"
    r"Script)(_|$)"
)


def is_data_name(name: str) -> bool:
    return bool(DATA_PREFIX.search(name)
                or DATA_SUFFIX.search(name)
                or SCRIPT_LABEL.search(name))


# A handful of routines on the ARM entry / interrupt path. pokefirered
# is THUMB except for the ROM header branch, crt0, and the IRQ handler.
# Address 0x08000000 is the ARM branch in the cart header; the rest the
# recompiler discovers by following it. Keep this list minimal and by
# NAME so it survives rebases.
ARM_NAME_HINTS = {"IntrMain", "Init"}
ARM_ADDR_SEEDS = {0x08000000}


def region_for(addr: int) -> str:
    if 0x00000000 <= addr <= 0x00003FFF:
        return "bios"
    if 0x02000000 <= addr <= 0x0203FFFF:
        return "ewram"
    if 0x03000000 <= addr <= 0x03007FFF:
        return "iwram"
    if 0x04000000 <= addr <= 0x040003FF:
        return "io"
    if 0x05000000 <= addr <= 0x050003FF:
        return "pal"
    if 0x06000000 <= addr <= 0x06017FFF:
        return "vram"
    if 0x07000000 <= addr <= 0x070003FF:
        return "oam"
    if 0x08000000 <= addr <= 0x09FFFFFF:
        return "rom"
    if 0x0A000000 <= addr <= 0x0BFFFFFF:
        return "rom_mirror"
    if 0x0E000000 <= addr <= 0x0E00FFFF:
        return "sram"
    return "unknown"


def classify(addr: int, size: int, name: str) -> tuple[str, str]:
    """Return (kind, mode). kind in {func, data}; mode in {arm, thumb}."""
    region = region_for(addr)
    if region not in ("rom", "rom_mirror"):
        return "data", "thumb"
    if size == 0:
        # size-0 ROM symbols are overwhelmingly sub-labels into data
        # (script bytecode, tables, header fields). Treat as data.
        return "data", "thumb"
    if is_data_name(name):
        return "data", "thumb"
    mode = "arm" if (name in ARM_NAME_HINTS or addr in ARM_ADDR_SEEDS) \
        else "thumb"
    return "func", mode


def parse_sym(path: pathlib.Path) -> list[Sym]:
    out: list[Sym] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    seen: set[tuple[int, str]] = set()
    for line in text.splitlines():
        m = SYM_LINE.match(line)
        if not m:
            continue
        raw_addr = int(m.group(1), 16)
        binding = m.group(2)
        size = int(m.group(3), 16)
        name = m.group(4)
        addr = raw_addr & ~1  # strip THUMB interworking bit if present
        kind, mode = classify(addr, size, name)
        key = (addr, name)
        if key in seen:
            continue
        seen.add(key)
        out.append(Sym(addr=addr, size=size, binding=binding, name=name,
                       region=region_for(addr), kind=kind, mode=mode))
    return out


# ── writers ──────────────────────────────────────────────────────────

_HDR = "# Generated by tools/import_pokefirered_symbols/" \
       "import_pokefirered_symbols.py\n"
_SRC = "# Source: pret/pokefirered `symbols` branch .sym " \
       "(names + addresses + sizes only).\n"


def write_imported_symbols(path: pathlib.Path, funcs: list[Sym]) -> int:
    rows = sorted(funcs, key=lambda s: (s.addr, s.name))
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Tab-separated: addr\tmode\tname\n")
        fh.write(_HDR)
        fh.write(_SRC)
        fh.write("# Heuristic function set (size>0, non-data name). The\n")
        fh.write("# recompiler is the authority; this is a seed.\n")
        for s in rows:
            fh.write(f"0x{s.addr:08X}\t{s.mode}\t{s.name}\n")
    return len(rows)


def write_function_boundaries(path: pathlib.Path, funcs: list[Sym]) -> int:
    rows = sorted(funcs, key=lambda s: (s.addr, s.name))
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Tab-separated: start\tend\tmode\tname\n")
        fh.write(_HDR)
        fh.write("# end = start + size - 1 (size is exact, from the .sym).\n")
        for s in rows:
            end = s.addr + s.size - 1 if s.size else 0
            fh.write(f"0x{s.addr:08X}\t0x{end:08X}\t{s.mode}\t{s.name}\n")
    return len(rows)


def write_data_symbols(path: pathlib.Path, data: list[Sym]) -> int:
    rows = sorted(data, key=lambda s: (s.addr, s.name))
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Tab-separated: addr\tregion\tname\n")
        fh.write(_HDR)
        fh.write(_SRC)
        for s in rows:
            fh.write(f"0x{s.addr:08X}\t{s.region}\t{s.name}\n")
    return len(rows)


def write_all_symbols(path: pathlib.Path, syms: list[Sym]) -> int:
    rows = sorted(syms, key=lambda s: (s.addr, s.name))
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Tab-separated: addr\tsize\tregion\tkind\tmode\tname\n")
        fh.write(_HDR)
        fh.write("# LOSSLESS master list of every .sym entry. kind/mode are\n")
        fh.write("# the heuristic classification; re-derive freely.\n")
        for s in rows:
            fh.write(f"0x{s.addr:08X}\t0x{s.size:08X}\t{s.region}\t"
                     f"{s.kind}\t{s.mode}\t{s.name}\n")
    return len(rows)


def write_recompiler_toml(path: pathlib.Path, funcs: list[Sym],
                          rom_path: pathlib.Path) -> int:
    rom_sha1 = ""
    rom_size = 0
    if rom_path.exists():
        rom = rom_path.read_bytes()
        rom_sha1 = hashlib.sha1(rom).hexdigest()
        rom_size = len(rom)
    else:
        print(f"  warn: ROM not found at {rom_path}; "
              f"sha1/size left blank in TOML", file=sys.stderr)

    rows = sorted(funcs, key=lambda s: (s.addr, s.name))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write(_HDR)
        fh.write("# Source: pret/pokefirered .sym plus ROM header metadata.\n")
        fh.write("# Do not hand-edit; rerun the importer.\n\n")

        fh.write("[program]\n")
        fh.write('name = "Pokemon FireRed Version (USA)"\n')
        fh.write('id = "firered_usa"\n')
        fh.write("load_address = 0x08000000\n")
        fh.write(f"size = 0x{rom_size:08X}\n")
        fh.write("entry_pc = 0x08000000\n\n")

        fh.write("[identity]\n")
        fh.write(f'sha1 = "{rom_sha1}"\n\n')

        fh.write("[[data_range]]\n")
        fh.write("start = 0x08000004\n")
        fh.write("end = 0x080000C0\n")
        fh.write('note = "GBA ROM header and Nintendo logo bytes"\n\n')

        for s in rows:
            fh.write("[[extra_func]]\n")
            fh.write(f"addr = 0x{s.addr:08X}\n")
            fh.write(f'mode = "{s.mode}"\n')
            fh.write(f'name = "{s.name}"\n\n')

    return len(rows)


def write_ghidra_script(path: pathlib.Path, funcs: list[Sym],
                        data: list[Sym]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write(GHIDRA_SCRIPT_HEADER)
        fh.write("\n# ── Functions ───────────────────────────────\n")
        fh.write("FUNCTIONS = [\n")
        for s in sorted(funcs, key=lambda s: s.addr):
            fh.write(f"    (0x{s.addr:08X}, {s.mode!r}, {s.name!r}),\n")
        fh.write("]\n\n")
        fh.write("# ── Data symbols ────────────────────────────\n")
        fh.write("DATA_SYMS = [\n")
        for s in sorted(data, key=lambda s: s.addr):
            fh.write(f"    (0x{s.addr:08X}, {s.region!r}, {s.name!r}),\n")
        fh.write("]\n\n")
        fh.write(GHIDRA_SCRIPT_BODY)


GHIDRA_SCRIPT_HEADER = '''# import_symbols.py — Ghidra Jython script.
#
# Applies pokefirered-derived function and data symbols to the current
# program (the cart loaded at 0x08000000). Use from Ghidra's Script
# Manager.
#
# Generated by tools/import_pokefirered_symbols. Do not hand-edit.

# @author tools/import_pokefirered_symbols
# @category Symbol Import
# @keybinding
# @menupath
# @toolbar

from ghidra.program.model.symbol import SourceType
from ghidra.program.model.address import AddressOutOfBoundsException
'''


GHIDRA_SCRIPT_BODY = '''
def _addr(value):
    return currentAddress.getAddressSpace().getAddress(value)


def _apply_function(addr_int, mode, name):
    try:
        addr = _addr(addr_int)
    except AddressOutOfBoundsException:
        return False
    if not currentProgram.getMemory().contains(addr):
        return False
    fm = currentProgram.getFunctionManager()
    existing = fm.getFunctionAt(addr)
    if existing is None:
        try:
            createFunction(addr, name)
        except Exception as exc:
            print("createFunction failed at %s (%s): %s" % (addr, name, exc))
            return False
    else:
        existing.setName(name, SourceType.USER_DEFINED)
    try:
        tmode = currentProgram.getProgramContext().getRegister("TMode")
        if tmode is not None:
            currentProgram.getProgramContext().setValue(
                tmode, addr, addr,
                java.math.BigInteger.valueOf(1 if mode == "thumb" else 0))
    except Exception:
        pass
    return True


def _apply_label(addr_int, name):
    try:
        addr = _addr(addr_int)
    except AddressOutOfBoundsException:
        return False
    if not currentProgram.getMemory().contains(addr):
        return False
    currentProgram.getSymbolTable().createLabel(
        addr, name, SourceType.USER_DEFINED)
    return True


def main():
    fn_ok = fn_skip = 0
    for addr_int, mode, name in FUNCTIONS:
        if _apply_function(addr_int, mode, name):
            fn_ok += 1
        else:
            fn_skip += 1
    d_ok = d_skip = 0
    for addr_int, region, name in DATA_SYMS:
        if _apply_label(addr_int, name):
            d_ok += 1
        else:
            d_skip += 1
    print("Functions: %d applied, %d skipped" % (fn_ok, fn_skip))
    print("Data symbols: %d applied, %d skipped" % (d_ok, d_skip))


import java.math
main()
'''


# ── main ───────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sym", type=pathlib.Path,
                    default=ROOT / "third_party" / "pokefirered"
                    / "pokefirered.sym",
                    help="Path to the vendored .sym map.")
    ap.add_argument("--rom", type=pathlib.Path,
                    default=ROOT / "roms" / "firered_usa.gba",
                    help="ROM path used only to hash-anchor the TOML.")
    ap.add_argument("--out", type=pathlib.Path,
                    default=ROOT / "symbols",
                    help="Output dir for symbol TSVs.")
    ap.add_argument("--ghidra", type=pathlib.Path,
                    default=ROOT / "ghidra",
                    help="Output dir for the Ghidra import script.")
    ap.add_argument("--toml", type=pathlib.Path,
                    default=ROOT / "symbols" / "firered.toml",
                    help="Output path for the gba_recompile TOML seed.")
    args = ap.parse_args()

    if not args.sym.exists():
        print(f"error: .sym not found at {args.sym}", file=sys.stderr)
        print("Get it from pret/pokefirered's `symbols` branch:",
              file=sys.stderr)
        print("  git -C third_party/pokefirered fetch --depth 1 origin "
              "symbols", file=sys.stderr)
        print("  git -C third_party/pokefirered cat-file -p "
              "FETCH_HEAD:pokefirered.sym > "
              "third_party/pokefirered/pokefirered.sym", file=sys.stderr)
        return 1

    print(f"==> parsing {args.sym}")
    syms = parse_sym(args.sym)
    funcs = [s for s in syms if s.kind == "func"]
    data = [s for s in syms if s.kind == "data"]
    print(f"    {len(syms)} symbols: {len(funcs)} func / {len(data)} data")

    args.out.mkdir(parents=True, exist_ok=True)
    args.ghidra.mkdir(parents=True, exist_ok=True)

    n1 = write_imported_symbols(args.out / "imported_symbols.tsv", funcs)
    n2 = write_function_boundaries(
        args.out / "function_boundaries.tsv", funcs)
    n3 = write_data_symbols(args.out / "imported_data_symbols.tsv", data)
    n4 = write_all_symbols(args.out / "all_symbols.tsv", syms)
    n5 = write_recompiler_toml(args.toml, funcs, args.rom)
    write_ghidra_script(args.ghidra / "import_symbols.py", funcs, data)

    print(f"==> imported_symbols.tsv     {n1}")
    print(f"==> function_boundaries.tsv  {n2}")
    print(f"==> imported_data_symbols.tsv {n3}")
    print(f"==> all_symbols.tsv          {n4}")
    print(f"==> firered.toml             {n5} extra_func")
    print(f"==> ghidra/import_symbols.py written")

    by_region: dict[str, int] = defaultdict(int)
    for s in funcs:
        by_region[s.region] += 1
    arm = sum(1 for s in funcs if s.mode == "arm")
    print(f"    funcs by region: {dict(sorted(by_region.items()))}")
    print(f"    funcs by mode: arm={arm} thumb={len(funcs) - arm}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
