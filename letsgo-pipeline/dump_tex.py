"""Export every BNTX texture inside the given .gfpak files to PNGs in outdir."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gfpak, bntx
outdir = sys.argv[1]
total = 0
for p in sys.argv[2:]:
    for h, data in gfpak.read(p):
        if data[:4] == b'BNTX':
            total += len(bntx.export(data, outdir))
print('exported', total, 'textures to', outdir)
