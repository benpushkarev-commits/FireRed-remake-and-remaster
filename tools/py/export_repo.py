"""python tools/py/export_repo.py OUT_DIR

Stage a clean, flattened copy of the project for the private GitHub repo
(OUT_DIR keeps its .git, so re-running then committing updates the repo).
Copies: tracked + non-ignored files of the game repo and its gbarecomp /
recomp-ui submodules, plus the project root (docs, tools, deps, scripts).
Never copies ROMs, saves, BIOS, generated game code, build output or test saves."""
import os, pathlib, shutil, subprocess, sys

root = pathlib.Path(r"C:\Users\benpu\Desktop\FireRedRemaster-Project")
out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for item in out.iterdir():   # refresh everything but the repo history
    if item.name == ".git": continue
    shutil.rmtree(item) if item.is_dir() else item.unlink()

BAD_EXT = {".gba", ".sav", ".srm", ".fla", ".flash", ".state", ".state1", ".state2", ".state3", ".bmp"}
def bad(rel: str, size: int) -> str:
    p = rel.replace("\\", "/").lower()
    ext = os.path.splitext(p)[1]
    if ext in BAD_EXT or any(p.endswith(e) for e in (".state1", ".state2", ".state3")): return "rom/save/state/capture"
    if "bios" in os.path.basename(p) and ext == ".bin": return "bios"
    if "/generated/" in p and not p.endswith("readme.md"): return "generated game code"
    if size > 50 * 1024 * 1024: return "over 50 MB"
    return ""

copied, skipped, total = 0, [], 0
def copy(src: pathlib.Path, rel: str):
    global copied, total
    size = src.stat().st_size
    why = bad(rel, size)
    if why: skipped.append((rel, why)); return
    dst = out / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    copied += 1; total += size

def repo_files(repo: pathlib.Path):
    r = subprocess.run(["git", "-C", str(repo), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                       capture_output=True, check=True)
    return [f for f in r.stdout.decode("utf-8").split("\0") if f]

game = root / "FireRedLeafGreenRecomp"
subs = {"gbarecomp", "recomp-ui", "third_party/pokefirered"}
for repo, prefix in [(game, "FireRedLeafGreenRecomp"), (game / "gbarecomp", "FireRedLeafGreenRecomp/gbarecomp"),
                     (game / "recomp-ui", "FireRedLeafGreenRecomp/recomp-ui")]:
    for f in repo_files(repo):
        if repo == game and (f in subs or f == ".gitmodules"): continue
        src = repo / f
        if src.is_file(): copy(src, f"{prefix}/{f}")

SKIP_DIRS = {"FireRedLeafGreenRecomp", "build", "graphify-out", ".git"}
for dirpath, dirnames, filenames in os.walk(root):
    d = pathlib.Path(dirpath)
    rel_d = d.relative_to(root).as_posix()
    if rel_d == ".":
        dirnames[:] = [x for x in dirnames if x not in SKIP_DIRS]
    else:
        dirnames[:] = [x for x in dirnames if not (rel_d == "tools" and (x == "check-game" or x.startswith("firered-diorama-")))
                       and x != "__pycache__"]
    for fn in filenames:
        rel = (pathlib.Path(rel_d) / fn).as_posix() if rel_d != "." else fn
        if rel == ".claude/settings.local.json": continue
        copy(d / fn, rel)

print(f"copied {copied} files, {total / 1e6:.1f} MB")
from collections import Counter
print("skipped:", dict(Counter(w for _, w in skipped)))
for rel, why in skipped:
    if why not in ("generated game code",): print("  ", why, rel)
