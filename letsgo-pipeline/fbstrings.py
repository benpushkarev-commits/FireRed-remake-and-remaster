"""List plausible FlatBuffers strings (u32 len + bytes + NUL) in a file, with offsets."""
import sys, struct, re
d = open(sys.argv[1], 'rb').read()
lim = int(sys.argv[2]) if len(sys.argv) > 2 else 400
n = 0
for m in re.finditer(rb'[\x20-\x7e]{3,}\x00', d):
    s = m.start()
    if s >= 4 and s % 4 == 0 and struct.unpack_from('<I', d, s - 4)[0] == m.end() - 1 - s:
        print(hex(s - 4), d[s:m.end() - 1].decode())
        n += 1
        if n >= lim: break
