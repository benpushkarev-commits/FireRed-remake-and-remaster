"""Import complete core tables from the pinned source; never link C at runtime."""
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / 'reference/pokefirered'
REV = '037335f4c725d7c9aecdac87066f2002b4bd7e14'
assert subprocess.check_output(['git', '-C', str(REF), 'rev-parse', 'HEAD'], text=True).strip() == REV
sources = {}

def read(path):
    raw = (REF / path).read_bytes()
    sources[path] = hashlib.sha256(raw).hexdigest()
    return raw.decode()

headers = ['global', 'pokemon', 'species', 'moves', 'items', 'abilities']
prefix = '#define FIRERED 1\n#define TRUE 1\n#define FALSE 0\n' + ''.join(
    '#include "constants/' + h + '.h"\n' for h in headers)
for h in headers:
    read('include/constants/' + h + '.h')

def cpp(text):
    env = dict(os.environ, PATH='C:/msys64/mingw64/bin;' + os.environ['PATH'])
    return subprocess.check_output(['C:/msys64/mingw64/bin/gcc.exe', '-E', '-P', '-x', 'c',
        '-I', str(REF / 'include'), '-'], input=prefix + text, text=True, env=env)

def entries(text):
    result = []
    for match in re.finditer(r'\[(\d+)\]\s*=\s*\{', text):
        start = match.end()
        depth, end = 1, start
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        result.append((match[1], text[start:end-1]))
    return result

species_raw = read('src/data/pokemon/species_info.h')
species = {}
for sid, body in entries(cpp(species_raw)):
    fields = dict(re.findall(r'\.(\w+)\s*=\s*(\{[^}]*\}|[^,]+)', body))
    if not fields:
        continue  # SPECIES_NONE is the original zero sentinel, never creatable.
    keys = ['baseHP', 'baseAttack', 'baseDefense', 'baseSpeed', 'baseSpAttack', 'baseSpDefense']
    species[sid] = dict(base=[int(fields[k]) for k in keys],
        growth=int(fields['growthRate']), friendship=int(fields['friendship']),
        abilities=[int(v) for v in fields['abilities'].strip('{} ').split(',')], fields=fields)

learnsets = read('src/data/pokemon/level_up_learnsets.h')
sets = {name: [[int(level), move] for level, move in
    re.findall(r'LEVEL_UP_MOVE\((\d+),\s*(\w+)\)', body)]
    for name, body in re.findall(r'(s\w+LevelUpLearnset)\[\].*?\{(.*?)\};', learnsets, re.S)}
pointers = cpp(read('src/data/pokemon/level_up_learnset_pointers.h'))
pointer_pairs = re.findall(r'\[(\d+)\]\s*=\s*(s\w+LevelUpLearnset)', pointers)

moves = {}
for mid, body in entries(cpp(read('src/data/battle_moves.h'))):
    fields = dict(re.findall(r'\.(\w+)\s*=\s*([^,]+)', body))
    moves[mid] = dict(pp=int(fields['pp']), fields=fields)

items = json.loads(read('src/data/items.json'))['items']
symbols = []
for path in ['include/constants/species.h', 'include/constants/moves.h', 'include/constants/items.h']:
    symbols.extend(re.findall(r'^#define\s+((?:SPECIES|MOVE|ITEM)_\w+)\s', read(path), re.M))
experience_source = read('src/data/pokemon/experience_tables.h')
# Let the authoring compiler evaluate original C table expressions and symbolic
# aliases. This avoids duplicating growth formulas or losing reserved IDs.
authoring = '#include <stdio.h>\ntypedef unsigned int u32;\n' + prefix + experience_source
authoring += '\nint main(void) {\n' + '\n'.join(
    'printf("ID ' + name + ' %u\\n", (unsigned)(' + name + '));' for name in symbols)
authoring += '\nfor (int g=0;g<6;g++) for (int n=0;n<=100;n++) printf("EXP %d %d %u\\n",g,n,gExperienceTables[g][n]);\nreturn 0; }\n'
with tempfile.TemporaryDirectory(dir=ROOT/'tools', prefix='core-import-') as temp:
    exe = Path(temp)/'tables.exe'
    env = dict(os.environ, PATH='C:/msys64/mingw64/bin;' + os.environ['PATH'])
    subprocess.run(['C:/msys64/mingw64/bin/gcc.exe', '-x', 'c', '-I', str(REF/'include'),
        '-', '-o', str(exe)], input=authoring, text=True, env=env, check=True)
    resolved = subprocess.check_output([str(exe)], text=True, env=env)
ids = {name: int(value) for name, value in re.findall(r'^ID (\w+) (\d+)$', resolved, re.M)}
for sid, name in pointer_pairs:
    if sid in species and name in sets:
        species[sid]['learnset'] = [[level, ids[move]] for level, move in sets[name] if move in ids]
experience = [[0]*101 for _ in range(6)]
for g, n, value in re.findall(r'^EXP (\d+) (\d+) (\d+)$', resolved, re.M):
    experience[int(g)][int(n)] = int(value)
# The original table is positional. Reserved entries repeat ITEM_NONE; using
# itemId as a dictionary key would silently erase those source slots.
item_data = {str(index): item for index, item in enumerate(items)}
pokemon = read('src/pokemon.c')
natures = [[int(n) for n in row.split(',')] for row in re.findall(
    r'\[NATURE_\w+\]\s*=\s*\{([^}]+)\}', pokemon)]

for path in ['src/item.c', 'src/event_data.c', 'src/new_game.c', 'include/random.h', 'src/random.c',
             'include/constants/flags.h', 'include/constants/vars.h']:
    read(path)
assert len(species) == 411 and len(moves) == 356 and len(natures) == 25
assert all('learnset' in s for s in species.values())
data = dict(revision=REV, species=species, moves=moves, items=item_data, ids=ids,
    natures=natures, experience=experience)
(ROOT/'data/core.json').write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')
(ROOT/'data/core-provenance.json').write_text(json.dumps(dict(revision=REV, sources=sources,
    status='Imported tables; new runtime behaviour unverified against original C.',
    counts=dict(species=len(species), moves=len(moves), items=len(item_data))), indent=2)+'\n')
print(f'Imported {len(species)} species entries, {len(moves)} moves, {len(item_data)} items and six growth tables.')
