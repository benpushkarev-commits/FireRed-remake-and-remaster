"""Terrain helpers for fitting Let's Go field models onto FireRed tile maps.

- heightmap(): rasterise the ground surface (no trees / grass cards / props) into a
  FireRed-tile grid so the slice can stand characters and kit pieces on Let's Go terrain.
- tree_filter(): per-tree culling so Let's Go trees only stand where FireRed has a
  blocking tile (the walkable layout stays FireRed's).
"""
import math, json
import numpy as np
from gf2glb import decode_vertices

NOT_GROUND = ('tree', 'miki', 'shadow', 'grass_s', 'flower', 'signboard', 'sky', 'fence', 'house', 'window', 'door', 'rooftile', 'wave')

def is_ground(name):
    return not any(k in name for k in NOT_GROUND)

def heightmap(m, root, width, height, res=4):
    """Return (res, grid[h*res][w*res]) of ground heights in tiles (None = no ground)."""
    tx, tz, s = root
    W, H = width * res, height * res
    grid = np.full((H, W), -1e9, dtype=np.float32)
    for grp in m['groups']:
        mesh = m['meshes'][grp['mesh']]
        n, cols = decode_vertices(mesh)
        pts = np.array([(p[0] * s + tx, p[1] * s, p[2] * s + tz) for p in cols[0]], dtype=np.float32)
        for poly in mesh['polys']:
            name = m['materials'][poly['material']]['name'] or ''
            if not is_ground(name):
                continue
            f = poly['faces']
            for t in range(0, len(f) - 2, 3):
                a, b, c = pts[f[t]], pts[f[t + 1]], pts[f[t + 2]]
                # tile-centre samples: grid cell (i, j) <-> tile coords ((j + 0.5)/res - 0.5, ...)
                xs = (np.array([a[0], b[0], c[0]]) + 0.5) * res - 0.5
                zs = (np.array([a[2], b[2], c[2]]) + 0.5) * res - 0.5
                x0, x1 = max(0, int(math.floor(xs.min()))), min(W - 1, int(math.ceil(xs.max())))
                z0, z1 = max(0, int(math.floor(zs.min()))), min(H - 1, int(math.ceil(zs.max())))
                if x0 > x1 or z0 > z1:
                    continue
                d = (zs[1] - zs[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (zs[0] - zs[2])
                if abs(d) < 1e-9:
                    continue
                for j in range(z0, z1 + 1):
                    for i in range(x0, x1 + 1):
                        l1 = ((zs[1] - zs[2]) * (i - xs[2]) + (xs[2] - xs[1]) * (j - zs[2])) / d
                        l2 = ((zs[2] - zs[0]) * (i - xs[2]) + (xs[0] - xs[2]) * (j - zs[2])) / d
                        l3 = 1 - l1 - l2
                        if l1 < -1e-4 or l2 < -1e-4 or l3 < -1e-4:
                            continue
                        y = l1 * a[1] + l2 * b[1] + l3 * c[1]
                        if y > grid[j, i]:
                            grid[j, i] = y
    # Holes (no Let's Go ground, e.g. beyond the model edge): copy the nearest sample in the column,
    # then the row, so the map edge continues level instead of dropping to zero.
    hole = grid < -1e8
    for i in range(W):
        col = grid[:, i]
        valid = np.where(~hole[:, i])[0]
        if len(valid):
            for j in np.where(hole[:, i])[0]:
                col[j] = col[valid[np.argmin(np.abs(valid - j))]]
    hole = grid < -1e8
    for j in range(H):
        valid = np.where(~hole[j])[0]
        for i in np.where(hole[j])[0]:
            grid[j, i] = grid[j, valid[np.argmin(np.abs(valid - i))]] if len(valid) else 0.0
    return res, [[round(float(v), 3) for v in row] for row in grid]

def tree_filter(m, root, tiles):
    """Return drop(material_name, (x, z) model centroid) culling Let's Go trees on FireRed walkable tiles."""
    tx, tz, s = root
    trunks = []
    for grp in m['groups']:
        mesh = m['meshes'][grp['mesh']]
        n, cols = decode_vertices(mesh)
        for poly in mesh['polys']:
            if 'miki' not in (m['materials'][poly['material']]['name'] or ''):
                continue
            f = poly['faces']
            for t in range(0, len(f) - 2, 3):
                cx = sum(cols[0][v][0] for v in f[t:t + 3]) / 3
                cz = sum(cols[0][v][2] for v in f[t:t + 3]) / 3
                for k in trunks:
                    if math.hypot(k[0] - cx, k[1] - cz) < 60:
                        break
                else:
                    trunks.append((cx, cz))
    def blocking(x, z):
        i, j = int(round(x * s + tx)), int(round(z * s + tz))
        if j < 0 or j >= len(tiles) or i < 0 or i >= len(tiles[0]):
            return True   # outside the FireRed map: keep the Let's Go backdrop
        return tiles[j][i] == 'X'
    keep = [blocking(x, z) for x, z in trunks]
    def drop(name, c):
        if not any(k in name for k in ('tree', 'miki', 'shadow')):
            return False
        best = min(range(len(trunks)), key=lambda i: math.hypot(trunks[i][0] - c[0], trunks[i][1] - c[1]), default=None)
        if best is None or math.hypot(trunks[best][0] - c[0], trunks[best][1] - c[1]) > 200:
            return False
        return not keep[best]
    return drop, trunks, keep
