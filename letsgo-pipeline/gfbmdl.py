"""Let's Go .gfbmdl (FlatBuffers) reader, schema from Switch Toolbox gfbmdl.fbs."""
import struct

class T:
    """FlatBuffers table view."""
    __slots__ = ('d', 'p', 'vt', 'vl')
    def __init__(self, d, p):
        self.d, self.p = d, p
        self.vt = p - struct.unpack_from('<i', d, p)[0]
        self.vl = struct.unpack_from('<H', d, self.vt)[0]
    def off(self, i):
        o = 4 + 2 * i
        if o >= self.vl: return 0
        return struct.unpack_from('<H', self.d, self.vt + o)[0]
    def scalar(self, i, fmt, default=0):
        o = self.off(i)
        return struct.unpack_from('<' + fmt, self.d, self.p + o)[0] if o else default
    def struct_(self, i, fmt):
        o = self.off(i)
        return struct.unpack_from('<' + fmt, self.d, self.p + o) if o else None
    def _ind(self, i):
        o = self.off(i)
        if not o: return None
        q = self.p + o
        return q + struct.unpack_from('<I', self.d, q)[0]
    def table(self, i):
        q = self._ind(i)
        return T(self.d, q) if q is not None else None
    def string(self, i):
        q = self._ind(i)
        if q is None: return None
        n = struct.unpack_from('<I', self.d, q)[0]
        return self.d[q + 4:q + 4 + n].decode('utf8', 'replace')
    def _vec(self, i):
        q = self._ind(i)
        if q is None: return 0, 0
        return struct.unpack_from('<I', self.d, q)[0], q + 4
    def tables(self, i):
        n, b = self._vec(i)
        return [T(self.d, b + 4 * k + struct.unpack_from('<I', self.d, b + 4 * k)[0]) for k in range(n)]
    def strings(self, i):
        out = []
        n, b = self._vec(i)
        for k in range(n):
            q = b + 4 * k + struct.unpack_from('<I', self.d, b + 4 * k)[0]
            m = struct.unpack_from('<I', self.d, q)[0]
            out.append(self.d[q + 4:q + 4 + m].decode('utf8', 'replace'))
        return out
    def scalars(self, i, fmt):
        n, b = self._vec(i)
        return list(struct.unpack_from('<%d%s' % (n, fmt), self.d, b)) if n else []
    def bytes_(self, i):
        n, b = self._vec(i)
        return self.d[b:b + n]

FMT_SIZE = {0: 4, 1: 2, 3: 1, 5: 2, 8: 1}

def load(d):
    root = T(d, struct.unpack_from('<I', d, 0)[0])
    m = dict(textures=root.strings(2), shaders=root.strings(3), material_names=root.strings(5))
    mats = []
    for t in root.tables(6):
        mat = dict(name=t.string(0), shader=t.string(1), maps=[], switches={}, values={}, colors={})
        for tm in t.tables(11):
            mat['maps'].append((tm.string(0), tm.scalar(1, 'i', 0)))
        for s in t.tables(12):
            mat['switches'][s.string(0)] = bool(s.scalar(1, 'B'))
        for s in t.tables(13):
            mat['values'][s.string(0)] = s.scalar(1, 'f')
        for s in t.tables(14):
            mat['colors'][s.string(0)] = s.struct_(1, 'fff')
        common = t.table(20)
        if common:
            for s in common.tables(0):
                mat['switches'].setdefault(s.string(0), bool(s.scalar(1, 'B')))
            for s in common.tables(1):
                mat['values'].setdefault(s.string(0), s.scalar(1, 'i'))
        mats.append(mat)
    m['materials'] = mats
    m['groups'] = [dict(bone=g.scalar(0, 'I'), mesh=g.scalar(1, 'I'), layer=g.scalar(3, 'I')) for g in root.tables(7)]
    meshes = []
    for t in root.tables(8):
        polys = [dict(material=p.scalar(0, 'I'), faces=p.scalars(1, 'H')) for p in t.tables(0)]
        attrs = [dict(type=a.scalar(0, 'I'), fmt=a.scalar(1, 'I'), count=a.scalar(2, 'I')) for a in t.tables(1)]
        meshes.append(dict(polys=polys, attrs=attrs, data=t.bytes_(2)))
    m['meshes'] = meshes
    bones = []
    for t in root.tables(9):
        bones.append(dict(name=t.string(0), type=t.scalar(1, 'I'), parent=t.scalar(2, 'i', -1),
                          scale=t.struct_(5, 'fff') or (1, 1, 1), rot=t.struct_(6, 'fff') or (0, 0, 0),
                          trans=t.struct_(7, 'fff') or (0, 0, 0), rigid=t.off(10) != 0))
    m['bones'] = bones
    return m
