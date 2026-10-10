"""Render a Let's Go field model top-down in a FireRed map's tile frame with the grid overlay.
usage: fit_check.py area model fr_map tx tz [out.png]"""
import sys, os, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import json
area, model, fr, tx, tz = sys.argv[1:6]
out = sys.argv[6] if len(sys.argv) > 6 else r'C:\Users\benpu\lgpe-work\out\fit_%s.png' % fr
w = json.load(open(r'C:\Users\benpu\Desktop\FireRedRemaster-Project\rust-prototype\data\world.json', encoding='utf8'))
m = [v for v in w['maps'] if v['id'] == fr][0]
W, H = len(m['tiles'][0]), len(m['tiles'])
glb = r'C:\Users\benpu\lgpe-work\out\%s.glb' % model
if not os.path.exists(glb):
    subprocess.run([sys.executable, 'field2glb.py', area, model, glb], cwd=os.path.dirname(os.path.abspath(__file__)), check=True)
bl = r'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
top = out[:-4] + '_raw.png'
subprocess.run([bl, '-b', '--python', 'render_top.py', '--', glb, top, tx, tz, '0.01', str(W), str(H)],
               cwd=os.path.dirname(os.path.abspath(__file__)), check=True, capture_output=True)
subprocess.run([sys.executable, 'overlay.py', top, fr, out], cwd=os.path.dirname(os.path.abspath(__file__)), check=True)
print(out, W, H)
