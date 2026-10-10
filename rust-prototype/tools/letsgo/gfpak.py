"""Minimal GFLXPACK (Let's Go .gfpak) reader: list / extract with magic-based extensions."""
import struct, sys, os
import lz4.block

MAGICS = {b'BNTX': '.bntx'}

def read(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'GFLXPACK', path
    ver, _, nfiles, nfolders, info_off, hash_off, folder_off = struct.unpack_from('<IIIIQQQ', d, 8)
    hashes = struct.unpack_from('<%dQ' % nfiles, d, hash_off)
    out = []
    for i in range(nfiles):
        o = info_off + i * 24
        unk, comp, dsize, csize, pad, off = struct.unpack_from('<HHIIIQ', d, o)
        raw = d[off:off + csize]
        if comp == 2:
            data = lz4.block.decompress(raw, uncompressed_size=dsize)
        elif comp == 0:
            data = raw
        else:
            raise ValueError('compression %d in %s' % (comp, path))
        out.append((hashes[i], data))
    return out

def ext_for(data):
    if data[:4] in MAGICS:
        return MAGICS[data[:4]]
    return '.bin'

if __name__ == '__main__':
    src, dst = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else None)
    for i, (h, data) in enumerate(read(src)):
        e = ext_for(data)
        print('%3d %016x %9d %s %s' % (i, h, len(data), e, data[:8].hex()))
        if dst:
            os.makedirs(dst, exist_ok=True)
            open(os.path.join(dst, '%03d_%016x%s' % (i, h, e)), 'wb').write(data)
