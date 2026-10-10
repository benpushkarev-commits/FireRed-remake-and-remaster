"""Let's Go .gfbanm reader (schema/quaternion packing from Switch Toolbox gfbanim.cs / GFBANM.cs).
Returns per-bone keyframe lists of local scale / rotation (quaternion xyzw) / translation."""
import struct, math
from gfbmdl import T

def unpack_s15(u):
    sign = (u >> 14) & 1
    u &= 0x3FFF
    if sign == 0:
        u -= 0x4000
    return u

SWIZ = ((0, 3, 2, 1), (3, 0, 2, 1), (3, 2, 0, 1), (3, 2, 1, 0))
MAXV = 1 / (0x399E * math.sqrt(2.0))

def packed_quat(xv, yv, zv):
    """xv/yv/zv = the three stored ushorts in file order; returns (x, y, z, w)."""
    cq = ((zv & 0xFFFF) << 32) | ((yv & 0xFFFF) << 16) | (xv & 0xFFFF)
    extra = cq & 7
    num = cq >> 3
    B = (1 << 15) - 1
    x = unpack_s15((num >> 30) & B)
    y = unpack_s15((num >> 15) & B)
    z = unpack_s15(num & B)
    fx, fy, fz = x * MAXV, y * MAXV, z * MAXV
    q = [math.sqrt(max(0.0, 1 - fx * fx - fy * fy - fz * fz)), fx, fy, fz]
    m = SWIZ[extra & 3]
    r = [q[m[0]], q[m[1]], q[m[2]], q[m[3]]]
    if extra >> 2:
        r = [-v for v in r]
    return tuple(r)

def _union(t, type_slot, val_slot):
    kind = t.scalar(type_slot, 'B')
    return kind, (t.table(val_slot) if kind else None)

def _vec_track(kind, tt, nframes):
    if not kind or tt is None:
        return None
    if kind == 1:   # fixed
        v = tt.struct_(0, 'fff')
        return [(0, v)]
    if kind == 2:   # dynamic: one per frame
        n, b = tt._vec(0)
        return [(i, struct.unpack_from('<fff', tt.d, b + 12 * i)) for i in range(n)]
    # framed 16 / 8
    n, fb = tt._vec(0)
    fmt = 'H' if kind == 3 else 'B'
    frames = struct.unpack_from('<%d%s' % (n, fmt), tt.d, fb) if n else ()
    m, vb = tt._vec(1)
    return [(frames[i] if i < len(frames) else i, struct.unpack_from('<fff', tt.d, vb + 12 * i)) for i in range(m)]

def _rot_track(kind, tt):
    if not kind or tt is None:
        return None
    if kind == 1:
        return [(0, packed_quat(*tt.struct_(0, 'HHH')))]
    if kind == 2:
        n, b = tt._vec(0)
        return [(i, packed_quat(*struct.unpack_from('<HHH', tt.d, b + 6 * i))) for i in range(n)]
    n, fb = tt._vec(0)
    fmt = 'H' if kind == 3 else 'B'
    frames = struct.unpack_from('<%d%s' % (n, fmt), tt.d, fb) if n else ()
    m, vb = tt._vec(1)
    return [(frames[i] if i < len(frames) else i, packed_quat(*struct.unpack_from('<HHH', tt.d, vb + 6 * i))) for i in range(m)]

def load(d):
    root = T(d, struct.unpack_from('<I', d, 0)[0])
    cfg = root.table(0)
    frames = cfg.scalar(1, 'I') if cfg else 0
    fps = cfg.scalar(2, 'I') if cfg else 30
    bones = {}
    bl = root.table(1)
    if bl:
        for b in bl.tables(0):
            name = b.string(0)
            sk, st = _union(b, 1, 2)
            rk, rt = _union(b, 3, 4)
            tk, tt = _union(b, 5, 6)
            bones[name] = dict(scale=_vec_track(sk, st, frames), rot=_rot_track(rk, rt), trans=_vec_track(tk, tt, frames))
    return dict(frames=frames, fps=fps or 30, bones=bones)

def is_gfbanm(d):
    try:
        a = load(d)
        return a['frames'] > 0 and len(a['bones']) > 0
    except Exception:
        return False
