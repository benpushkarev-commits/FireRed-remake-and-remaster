"""Import the bounded opening-rival fixture and original AI script instructions.

No ROM/emulator is used at runtime. Unsupported AI instructions fail in the VM.
"""
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / 'reference/pokefirered'
REV = '037335f4c725d7c9aecdac87066f2002b4bd7e14'
assert subprocess.check_output(['git','-C',str(REF),'rev-parse','HEAD'],text=True).strip()==REV
sources = {}

def read(path):
    raw = (REF / path).read_bytes()
    sources[path] = hashlib.sha256(raw).hexdigest()
    return raw.decode()

def block(text, key):
    return re.search(r'\[' + key + r'\]\s*=\s*\{(.*?)\n\s*\}', text, re.S)[1]

species_text = read('src/data/pokemon/species_info.h')
moves_text = read('src/data/battle_moves.h')
learnsets = read('src/data/pokemon/level_up_learnsets.h')
trainer = block(read('src/data/trainers.h'), 'TRAINER_RIVAL_OAKS_LAB_SQUIRTLE')
party = read('src/data/trainer_parties.h')
assert re.search(r'sParty_RivalOaksLabSquirtle\[\].*?\.iv = 0,.*?\.lvl = 5,.*?SPECIES_SQUIRTLE', party, re.S)
assert 'AI_SCRIPT_CHECK_BAD_MOVE | AI_SCRIPT_TRY_TO_FAINT | AI_SCRIPT_CHECK_VIABILITY' in trainer
species = []
all_species_names = ['Charmander', 'Squirtle', 'Bulbasaur', 'Pidgey', 'Rattata', 'Caterpie', 'Metapod', 'Butterfree', 'Weedle', 'Kakuna', 'Beedrill', 'Pikachu', 'Spearow', 'Mankey', 'Geodude', 'Onix', 'Sandshrew', 'Zubat', 'Paras', 'Clefairy', 'Ekans', 'Nidoran_F', 'Nidoran_M', 'Jigglypuff', 'Abra', 'Bellsprout', 'Oddish', 'Staryu', 'Starmie', 'Horsea', 'Goldeen', 'Shellder', 'Pidgeotto', 'Raticate', 'Kadabra']
for name in all_species_names:
    b = block(species_text, 'SPECIES_' + name.upper())
    bases = [int(re.search(r'\.' + key + r' = (\d+)', b)[1]) for key in
             ['baseHP', 'baseAttack', 'baseDefense', 'baseSpeed', 'baseSpAttack', 'baseSpDefense']]
    ls = re.search(r's' + name.replace('_', '') + r'LevelUpLearnset\[\].*?\{(.*?)\};', learnsets, re.S)[1]
    moves = [m for level, m in re.findall(r'LEVEL_UP_MOVE\((\d+), (MOVE_\w+)\)', ls) if int(level) <= 5]
    if not moves:
        moves = [m for level, m in re.findall(r'LEVEL_UP_MOVE\((\d+), (MOVE_\w+)\)', ls)[:2]]
    exp_yield=int(re.search(r'\.expYield = (\d+)',b)[1])
    ev_yield=[int(re.search(r'\.'+key+r' = (\d+)',b)[1]) for key in
              ['evYield_HP','evYield_Attack','evYield_Defense','evYield_Speed','evYield_SpAttack','evYield_SpDefense']]
    species.append(dict(name=name, base=bases, moves=moves, exp_yield=exp_yield, ev_yield=ev_yield))
needed_moves = {'MOVE_SCRATCH', 'MOVE_GROWL', 'MOVE_TACKLE', 'MOVE_TAIL_WHIP', 'MOVE_SAND_ATTACK', 'MOVE_QUICK_ATTACK', 'MOVE_STRING_SHOT', 'MOVE_POISON_STING', 'MOVE_HARDEN', 'MOVE_THUNDER_SHOCK', 'MOVE_PECK', 'MOVE_LEER', 'MOVE_KARATE_CHOP', 'MOVE_LOW_KICK', 'MOVE_DEFENSE_CURL', 'MOVE_ROCK_THROW', 'MOVE_BIND', 'MOVE_ROCK_TOMB', 'MOVE_EMBER', 'MOVE_WATER_GUN', 'MOVE_VINE_WHIP', 'MOVE_LEECH_SEED', 'MOVE_LEECH_LIFE', 'MOVE_POUND', 'MOVE_WRAP', 'MOVE_SING', 'MOVE_TELEPORT', 'MOVE_ABSORB', 'MOVE_RAPID_SPIN', 'MOVE_RECOVER', 'MOVE_SWIFT', 'MOVE_BUBBLE', 'MOVE_WATER_SPORT', 'MOVE_WITHDRAW', 'MOVE_GUST', 'MOVE_KINESIS', 'MOVE_CONFUSION'}
moves = {}
for name in sorted(list(needed_moves)):
    b = block(moves_text, name)
    moves[name] = {k: int(re.search(r'\.' + k + r' = (\d+)', b)[1]) for k in ['power', 'accuracy', 'pp']}
    moves[name]['effect'] = re.search(r'\.effect = (\w+)', b)[1]

labels, instructions = {}, []
for raw in read('data/battle_ai_scripts.s').splitlines():
    line = raw.split('@')[0].strip()
    if not line or line.startswith(('.include', '.align', '.4byte', '.section')):
        continue
    if line.endswith(':'):
        labels[line.rstrip(':')] = len(instructions)
        continue
    op, _, args = line.partition(' ')
    if '\t' in op:
        op, args = line.split(None, 1)
    instructions.append([op, [s.strip() for s in args.split(',') if s.strip()]])

# Trainer personality uses encoded trainer+species names, not their ASCII bytes.
charmap = read('charmap.txt')
chars = {ch: int(code, 16) for ch, code in re.findall(r"^'(.)'\s*=\s*([0-9A-F]{2})\s*$", charmap, re.M)}
trainer_name = re.search(r'\.trainerName = _\("(.*?)"\)', trainer)[1]
personality = 0x88 + (sum(chars[c] for c in trainer_name + 'SQUIRTLE') << 8)
pokemon = read('src/pokemon.c')
natures = [[int(n) for n in row.split(',')] for row in re.findall(
    r'\[NATURE_\w+\]\s*=\s*\{([^}]+)\}', pokemon)]
assert len(natures) == 25
data = dict(revision=REV, species=species, moves=moves, labels=labels,
            instructions=instructions, natures=natures, rival_personality=personality,
            ai_roots=['AI_CheckBadMove', 'AI_CheckViability', 'AI_TryToFaint'])
experience=read('src/data/pokemon/experience_tables.h')
assert '#define EXP_MEDIUM_SLOW(n)((6 * CUBE(n)) / 5 - (15 * SQUARE(n)) + (100 * n) - 140)' in experience
data['experience']=[0,1]+[6*n**3//5-15*n*n+100*n-140 for n in range(2,101)]
(ROOT / 'data/battle.json').write_text(json.dumps(data, indent=2) + '\n')
(ROOT / 'data/battle-provenance.json').write_text(json.dumps(dict(
    revision=REV, sources=sources, trainer='TRAINER_RIVAL_OAKS_LAB_SQUIRTLE',
    player_fixture='Level 5 Charmander, Hardy, all IVs 31, all EVs 0; explicit test fixture',
    rival_personality=personality, rival_nature=personality % 25,
    runtime='Native Rust; source AI scripts are data, not ARM code'), indent=2) + '\n')
print('Imported original opening-rival species, four moves, trainer personality and AI scripts.')
