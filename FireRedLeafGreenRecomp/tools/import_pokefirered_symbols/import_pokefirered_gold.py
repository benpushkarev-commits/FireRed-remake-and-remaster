#!/usr/bin/env python3
"""import_pokefirered_gold.py — GOLD oracle importer.

Consumes a byte-matching pokefirered build's ELF metadata (produced in WSL,
see ../../../wsl_build_firered.sh) instead of the type-less .sym:

  symbols/pokefirered_readelf.txt   arm-none-eabi-readelf -sW pokefirered.elf
  symbols/pokefirered_sections.txt  arm-none-eabi-readelf -SW pokefirered.elf

The build is verified byte-exact to the real ROM (sha1 41cb23d8...), so every
address is correct, and readelf gives us what the .sym lacked:

  * EXACT function set   — STT_FUNC symbols (no heuristics).
  * EXACT ARM/THUMB mode — THUMB funcs carry bit-0 set in st_value
                           (readelf shows the raw value; nm masks it).
  * EXACT code/data split — section flags: executable (X) = code,
                           everything else in ROM = data. This is what was
                           missing before: the recompiler swept ~6.5 MB of
                           non-executable ROM (.rodata/gfx/song/...) as code,
                           producing 717k bogus entries / 684 MB. Marking the
                           non-executable ROM sections as [[data_range]] stops
                           that cold.

Outputs:
  symbols/firered.toml             gba_recompile config: extra_func + data_range
  symbols/imported_symbols.tsv     addr  mode  name      (functions)
  symbols/function_boundaries.tsv  start end  mode name  (exact, from size)
  symbols/imported_data_symbols.tsv addr region name     (OBJECT symbols)
  symbols/all_symbols.tsv          addr size region kind mode name (lossless)
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# readelf -sW symbol line:  "  539: 0800080d   56 FUNC   LOCAL  DEFAULT  3 Name"
SYM = re.compile(
    r"^\s*\d+:\s+([0-9A-Fa-f]+)\s+(\d+)\s+(\S+)\s+(\S+)\s+\S+\s+(\S+)\s+(.+?)\s*$"
)
# readelf -SW section line: "  [ 3] .text  PROGBITS  08000000 001000 15f9b4 00  AX 0 0 4"
SEC = re.compile(
    r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+"
    r"([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+([A-Za-z]*)"
)

ROM_LO, ROM_HI = 0x08000000, 0x09FFFFFF


def region_for(a: int) -> str:
    if 0x02000000 <= a <= 0x0203FFFF: return "ewram"
    if 0x03000000 <= a <= 0x03007FFF: return "iwram"
    if 0x08000000 <= a <= 0x09FFFFFF: return "rom"
    if 0x00000000 <= a <= 0x00003FFF: return "bios"
    return "other"


def parse_sections(path: pathlib.Path):
    """Return (code_ranges, data_ranges) of (start,end) for ROM PROGBITS."""
    code, data = [], []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = SEC.match(line)
        if not m:
            continue
        name, sectype, addr_s, size_s, flags = m.groups()
        if sectype != "PROGBITS":
            continue
        addr, size = int(addr_s, 16), int(size_s, 16)
        if size == 0 or not (ROM_LO <= addr <= ROM_HI):
            continue
        (code if "X" in flags else data).append((name, addr, addr + size))
    return code, data


def coalesce(ranges):
    """Merge contiguous/overlapping (start,end) ranges. ranges: (name,s,e)."""
    out = []
    for _name, s, e in sorted(ranges, key=lambda r: r[1]):
        if out and s <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], e))
        else:
            out.append((s, e))
    return out


def in_ranges(a: int, ranges) -> bool:
    for s, e in ranges:
        if s <= a < e:
            return True
    return False


def parse_symbols(path: pathlib.Path):
    funcs: dict[int, tuple[str, int, str]] = {}   # addr -> (mode,size,name)
    data: list[tuple[int, int, str]] = []          # (addr,size,name)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = SYM.match(line)
        if not m:
            continue
        value_s, size_s, typ, _bind, ndx, name = m.groups()
        name = name.strip()
        if not name or ndx in ("ABS", "UND", "COM"):
            continue
        value, size = int(value_s, 16), int(size_s)
        if typ == "FUNC":
            real = value & ~1
            mode = "thumb" if (value & 1) else "arm"
            # Prefer the first (GLOBAL tends to precede; dedupe by addr).
            funcs.setdefault(real, (mode, size, name))
        elif typ == "OBJECT" and ROM_LO <= value <= ROM_HI:
            data.append((value, size, name))
    return funcs, data


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--syms", type=pathlib.Path,
                    default=ROOT / "symbols" / "pokefirered_readelf.txt")
    ap.add_argument("--sections", type=pathlib.Path,
                    default=ROOT / "symbols" / "pokefirered_sections.txt")
    ap.add_argument("--rom", type=pathlib.Path,
                    default=ROOT / "roms" / "firered_usa.gba")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "symbols")
    ap.add_argument("--toml", type=pathlib.Path,
                    default=ROOT / "symbols" / "firered.toml")
    args = ap.parse_args()

    for p in (args.syms, args.sections):
        if not p.exists():
            print(f"error: missing {p}\n  (run wsl_build_firered.sh + "
                  f"wsl_dump_readelf.sh first)", file=sys.stderr)
            return 1

    code_secs, data_secs = parse_sections(args.sections)
    data_ranges = coalesce(data_secs)
    # Carve the ROM header (logo/title) out of .text as data too.
    header = (0x08000004, 0x080000C0)
    all_ranges = coalesce([("hdr", *header)] + [("d", s, e) for s, e in data_ranges])

    print("==> code sections:", [(n, hex(s), hex(e)) for n, s, e in code_secs])
    print("==> data ranges:  ", [(hex(s), hex(e)) for s, e in all_ranges])

    funcs, data = parse_symbols(args.syms)
    # Drop any function that lands inside a data range (safety; also the
    # recompiler rejects extra_func-inside-data_range as contradictory).
    dropped = [a for a in funcs if in_ranges(a, all_ranges)]
    for a in dropped:
        del funcs[a]
    arm = sum(1 for m, _, _ in funcs.values() if m == "arm")
    print(f"==> functions: {len(funcs)} "
          f"(thumb={len(funcs)-arm} arm={arm}), dropped-in-data={len(dropped)}")
    print(f"==> data OBJECT symbols (ROM): {len(data)}")

    rom_sha1 = rom_size = None
    if args.rom.exists():
        b = args.rom.read_bytes()
        rom_sha1, rom_size = hashlib.sha1(b).hexdigest(), len(b)

    args.out.mkdir(parents=True, exist_ok=True)
    frows = sorted(funcs.items())                      # (addr,(mode,size,name))

    # ---- firered.toml ----
    with args.toml.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Generated by import_pokefirered_gold.py from a byte-matching\n")
        fh.write("# pokefirered build (readelf). Do not hand-edit; rerun.\n\n")
        fh.write("[program]\n")
        fh.write('name = "Pokemon FireRed Version (USA)"\n')
        fh.write('id = "firered_usa"\n')
        fh.write("load_address = 0x08000000\n")
        fh.write(f"size = 0x{(rom_size or 0):08X}\n")
        fh.write("entry_pc = 0x08000000\n")
        fh.write("codegen_shards = 64\n\n")
        fh.write("[identity]\n")
        fh.write(f'sha1 = "{rom_sha1 or ""}"\n\n')
        for s, e in all_ranges:
            fh.write("[[data_range]]\n")
            fh.write(f"start = 0x{s:08X}\n")
            fh.write(f"end = 0x{e:08X}\n")
            fh.write('note = "non-executable ROM section (readelf)"\n\n')
        for addr, (mode, _size, name) in frows:
            fh.write("[[extra_func]]\n")
            fh.write(f"addr = 0x{addr:08X}\n")
            fh.write(f'mode = "{mode}"\n')
            fh.write(f'name = "{name}"\n\n')

    # ---- TSVs ----
    with (args.out / "imported_symbols.tsv").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# addr\tmode\tname  (STT_FUNC from byte-matching ELF)\n")
        for addr, (mode, _s, name) in frows:
            fh.write(f"0x{addr:08X}\t{mode}\t{name}\n")
    with (args.out / "function_boundaries.tsv").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# start\tend\tmode\tname  (end = start+size-1, exact)\n")
        for addr, (mode, size, name) in frows:
            end = addr + size - 1 if size else 0
            fh.write(f"0x{addr:08X}\t0x{end:08X}\t{mode}\t{name}\n")
    with (args.out / "imported_data_symbols.tsv").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("# addr\tregion\tname  (STT_OBJECT in ROM)\n")
        for addr, _size, name in sorted(data):
            fh.write(f"0x{addr:08X}\t{region_for(addr)}\t{name}\n")

    print(f"==> wrote {args.toml.name}: {len(frows)} extra_func + "
          f"{len(all_ranges)} data_range")
    print(f"==> sha1={rom_sha1} size=0x{(rom_size or 0):08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
