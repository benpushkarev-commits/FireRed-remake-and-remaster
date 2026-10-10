"""Minimal BNTX -> PNG (mip 0 only). Tegra block-linear deswizzle + BCn decode."""
import struct, sys, os
import texture2ddecoder as t2d
from PIL import Image

# format type (high byte) -> (block w/h, bytes per block, decoder or None for raw)
FMTS = {
    0x02: (1, 1, 'r8'), 0x0b: (1, 4, 'rgba8'), 0x0c: (1, 4, 'rgba8'),
    0x1a: (4, 8, t2d.decode_bc1), 
    0x1c: (4, 16, t2d.decode_bc3), 0x1d: (4, 8, t2d.decode_bc4),
    0x1e: (4, 16, t2d.decode_bc5), 0x20: (4, 16, t2d.decode_bc7),
}

def div_up(a, b): return (a + b - 1) // b

def deswizzle(data, wb, hb, bpp, block_h):
    out = bytearray(wb * hb * bpp)
    gobs_w = div_up(wb * bpp, 64)
    for y in range(hb):
        gy = (y // (8 * block_h)) * 512 * block_h * gobs_w + (y % (8 * block_h) // 8) * 512
        ry = ((y % 8) // 2) * 64 + (y % 2) * 16
        for xb in range(wb):
            x = xb * bpp
            a = gy + (x // 64) * 512 * block_h + ((x % 64) // 32) * 256 + ry + ((x % 32) // 16) * 32 + (x % 16)
            o = (y * wb + xb) * bpp
            out[o:o + bpp] = data[a:a + bpp]
    return bytes(out)

def name_at(d, off):
    n = struct.unpack_from('<H', d, off)[0]
    return d[off + 2:off + 2 + n].decode('utf8', 'replace')

def textures(d):
    assert d[:4] == b'BNTX'
    assert d[0x20:0x24] == b'NX  '
    count, info_arr = struct.unpack_from('<IQ', d, 0x24)
    for i in range(count):
        bi = struct.unpack_from('<Q', d, info_arr + i * 8)[0]
        assert d[bi:bi + 4] == b'BRTI', hex(bi)
        mips, = struct.unpack_from('<H', d, bi + 0x16)
        fmt, = struct.unpack_from('<I', d, bi + 0x1C)
        w, h = struct.unpack_from('<II', d, bi + 0x24)
        layout, = struct.unpack_from('<I', d, bi + 0x34)
        size, = struct.unpack_from('<I', d, bi + 0x50)
        name_off, _, ptrs = struct.unpack_from('<QQQ', d, bi + 0x60)
        mip0 = struct.unpack_from('<Q', d, ptrs)[0]
        yield dict(name=name_at(d, name_off), fmt=fmt, w=w, h=h, block_h=1 << (layout & 7),
                   data=d[mip0:mip0 + size])

def to_image(t):
    ft = t['fmt'] >> 8
    if ft not in FMTS:
        raise ValueError('unsupported format 0x%x' % t['fmt'])
    bw, bpp, dec = FMTS[ft]
    wb, hb = div_up(t['w'], bw), div_up(t['h'], bw)
    lin = deswizzle(t['data'], wb, hb, bpp, t['block_h'])
    if dec == 'rgba8':
        return Image.frombytes('RGBA', (t['w'], t['h']), lin)
    if dec == 'r8':
        return Image.frombytes('L', (t['w'], t['h']), lin)
    raw = dec(lin, wb * 4, hb * 4)
    # texture2ddecoder returns BGRA for every BCn format (checked against reference rips)
    im = Image.frombytes('RGBA', (wb * 4, hb * 4), raw, 'raw', 'BGRA')
    return im.crop((0, 0, t['w'], t['h']))

def export(d, outdir):
    os.makedirs(outdir, exist_ok=True)
    names = []
    for t in textures(d):
        try:
            to_image(t).save(os.path.join(outdir, t['name'] + '.png'))
            names.append(t['name'])
        except Exception as e:
            print('  skip', t['name'], hex(t['fmt']), e)
    return names
