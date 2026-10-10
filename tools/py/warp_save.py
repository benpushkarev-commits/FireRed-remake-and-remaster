#!/usr/bin/env python3
"""warp_save.py IN OUT GROUP NUM X Y — make Continue warp to (GROUP.NUM, X, Y).
Uses the game's own continue-game warp (SaveBlock2+9 bit0, SaveBlock1+0xC)."""
import struct, sys
class M:
    SECTOR, DATA, SIG = 0x1000, 3968, 0x08012025
    SB2_SIZE, SB1_SIZE, PSS_SIZE = 0xF24, 0x3D68, 0x83D0
    @staticmethod
    def chk(data):
        t = sum(struct.unpack('<%dI' % (len(data) // 4), data)) & 0xFFFFFFFF
        return ((t >> 16) + t) & 0xFFFF
    @staticmethod
    def sizes_for(sid):
        if sid == 0: return M.SB2_SIZE
        if 1 <= sid <= 4: return min(M.DATA, M.SB1_SIZE - (sid - 1) * M.DATA)
        return min(M.DATA, M.PSS_SIZE - (sid - 5) * M.DATA)
    @staticmethod
    def load_slot(buf):
        slots = []
        for slot in (0, 1):
            secs, ctr = {}, -1
            for s in range(slot * 14, slot * 14 + 14):
                sid, c, sig, cnt = struct.unpack_from('<HHII', buf, s * M.SECTOR + 0xFF4)
                if sig == M.SIG: secs[sid] = s; ctr = max(ctr, cnt)
            if len(secs) == 14: slots.append((ctr, slot, secs))
        return max(slots)
inp, out, g, n, x, y = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])
buf = bytearray(open(inp, 'rb').read())
ctr, slot, secs = M.load_slot(buf)
def fix(sid, mutate):
    s = secs[sid]; size = M.sizes_for(sid)
    data = bytearray(buf[s * M.SECTOR: s * M.SECTOR + size])
    mutate(data)
    buf[s * M.SECTOR: s * M.SECTOR + size] = data
    struct.pack_into('<H', buf, s * M.SECTOR + 0xFF6, M.chk(bytes(data)))
def sb2(d): d[9] |= 1
def sb1(d): d[0xC:0x14] = struct.pack('<bbbbhh', g, n, -1, 0, x, y)
fix(0, sb2); fix(1, sb1)
open(out, 'wb').write(buf)
print('slot', slot, 'warp ->', g, n, x, y)
