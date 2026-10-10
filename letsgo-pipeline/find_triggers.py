import json, sys
d = json.load(open(r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\data\actors.json', encoding='utf8'))
def walk(o, path=''):
    if isinstance(o, dict):
        if any('Pallet' in str(v) or 'Route1' in str(v) for v in o.values() if isinstance(v, str)) and ('var' in o or 'script' in o):
            print(path, {k: v for k, v in o.items() if not isinstance(v, (list, dict))})
        for k, v in o.items():
            walk(v, path + '/' + str(k))
    elif isinstance(o, list):
        for i, v in enumerate(o):
            walk(v, path + '[%d]' % i)
print(type(d).__name__, list(d.keys())[:20] if isinstance(d, dict) else len(d))
walk(d)
