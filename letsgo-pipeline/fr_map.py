import json, sys
w = json.load(open(r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\data\world.json', encoding='utf8'))
print(type(w).__name__, list(w.keys())[:12] if isinstance(w, dict) else len(w))
maps = w['maps'] if isinstance(w, dict) and 'maps' in w else w
if isinstance(maps, dict):
    print(list(maps.keys())[:30]); maps = list(maps.values())
print([m.get('id') for m in maps][:40])
key = sys.argv[1] if len(sys.argv) > 1 else 'Pallet'
m = [m for m in maps if key.lower() in str(m.get('id', '')).lower() and not m.get('indoor')][0]
print({k: (v if not isinstance(v, (list, dict)) else type(v).__name__ + str(len(v))) for k, v in m.items()})
for i, r in enumerate(m['tiles']): print('%2d %s' % (i, r))
for p in m['props']: print(p)
for n in m.get('npcs', []): print('npc', n.get('local_id'), n.get('at'), n.get('graphics'))
for k in ('warps', 'connections', 'exits'):
    if k in m: print(k, m[k])
