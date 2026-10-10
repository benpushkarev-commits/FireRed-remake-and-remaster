import sys, os, struct
for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            with open(p, 'rb') as h:
                b = h.read(32)
            print(os.path.relpath(p, root), os.path.getsize(p), b[:16].hex(), ''.join(chr(c) if 32 <= c < 127 else '.' for c in b))
