"""Import pinned original object templates and coordinate event metadata."""
import hashlib, json, re, subprocess, os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
REF = ROOT/'reference/pokefirered'
REV = '037335f4c725d7c9aecdac87066f2002b4bd7e14'
assert subprocess.check_output(['git','-C',str(REF),'rev-parse','HEAD'],text=True).strip() == REV
sources = {}
def read(path):
    raw = (REF/path).read_bytes()
    sources[path] = hashlib.sha256(raw).hexdigest()
    return raw.decode()
world = json.loads((ROOT/'data/world.json').read_text(encoding='utf-8'))
groups = json.loads(read('data/maps/map_groups.json'))
numbers = {name:[g,n] for g,group in enumerate(groups['group_order']) for n,name in enumerate(groups[group])}
originals = {m['id']:json.loads(read('data/maps/'+m['source']+'/map.json')) for m in world['maps']}
symbols = set()
for m in originals.values():
    for obj in m['object_events']:
        if obj.get('type') == 'clone': continue
        symbols.update([obj['flag'],obj['movement_type'],obj.get('trainer_type', '0')])
    for event in m['coord_events']:
        if event['type']=='trigger': symbols.update([event['var'],event['var_value']])
symbols = sorted(s for s in symbols if re.fullmatch(r'[A-Z][A-Z0-9_]*',s))
headers = ['constants/flags.h','constants/vars.h','constants/event_object_movement.h', 'constants/trainer_types.h']
source = '\n'.join('#include "'+h+'"' for h in headers)+'\n'
for h in headers: read('include/'+h)
source += '\n'.join('NATIVE_VALUE_'+s+' '+s for s in symbols)
process = subprocess.run(['C:/msys64/mingw64/bin/gcc.exe','-E','-P','-x','c','-I',str(REF/'include'),'-'],input=source,text=True,capture_output=True,env=dict(os.environ,PATH='C:/msys64/mingw64/bin;'+os.environ['PATH']))
if process.returncode: raise RuntimeError(process.stderr)
pre = process.stdout
values = {}
for name,expr in re.findall(r'^NATIVE_VALUE_(\w+) (.*)$',pre,re.M):
    if not re.fullmatch(r'[0-9a-fA-FxX()+ \-*/|&<>]+',expr): raise ValueError(expr)
    values[name] = int(eval(expr,{'__builtins__':{}},{}))
def value(s): return values[s] if s in values else int(s,0)
maps = {}
for m in world['maps']:
    orig = originals[m['id']]
    objects = []
    for i,obj in enumerate(orig['object_events'],1):
        if obj.get('type') == 'clone': continue
        objects.append(dict(local_id=i,at=[obj['x'],obj['y']],elevation=obj['elevation'],
            flag=value(obj['flag']),movement=value(obj['movement_type']),graphics=obj['graphics_id'],script=obj['script'],
            trainer_type=value(obj.get('trainer_type', '0')), trainer_sight=value(obj.get('trainer_sight_or_berry_tree_id', '0'))))
    coords = [dict(at=[e['x'],e['y']],elevation=e['elevation'],variable=value(e['var']),
        value=value(e['var_value']),script=e['script']) for e in orig['coord_events'] if e['type']=='trigger']
    maps[m['id']] = dict(number=numbers[m['source']],script=m['source']+'_MapScripts',objects=objects,coords=coords)
for path in ['src/script_movement.c','src/event_object_movement.c','src/script.c','src/scrcmd.c','src/field_control_avatar.c']:
    read(path)
(ROOT/'data/actors.json').write_text(json.dumps(dict(revision=REV,maps=maps),indent=2)+'\n')
(ROOT/'data/actors-provenance.json').write_text(json.dumps(dict(revision=REV,sources=sources,status='Native partial actor/scene port; detailed fidelity unverified.'),indent=2)+'\n')
print('Imported',sum(len(m['objects']) for m in maps.values()),'objects and',sum(len(m['coords']) for m in maps.values()),'coordinate triggers')
