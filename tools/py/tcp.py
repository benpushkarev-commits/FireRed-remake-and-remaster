#!/usr/bin/env python3
"""Tiny client for the game's debug TCP port (start it with tools\\run-debug.bat).
usage: tcp.py mem ADDR LEN          -> hex dump (ewram/iwram/vram by address)
       tcp.py u32 ADDR | u16 ADDR | u8 ADDR
       tcp.py io OFF LEN
       tcp.py raw '{"cmd":...}'
       tcp.py save PATH | load PATH
"""
import json, socket, sys

def cmd(obj):
    s = socket.create_connection(("127.0.0.1", 19842), timeout=10)
    s.sendall((json.dumps(obj) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(1 << 20)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode())

def region(addr):
    if 0x02000000 <= addr < 0x02040000: return "read_ewram"
    if 0x03000000 <= addr < 0x03008000: return "read_iwram"
    if 0x06000000 <= addr < 0x06018000: return "read_vram"
    if 0x05000000 <= addr < 0x05000400: return "read_pal"
    if 0x07000000 <= addr < 0x07000400: return "read_oam"
    if 0x08000000 <= addr < 0x0A000000: return "read_rom"
    raise SystemExit("bad addr %x" % addr)

def mem(addr, n):
    r = cmd({"cmd": region(addr), "addr": addr, "len": n})
    if not r.get("ok"):
        raise SystemExit(str(r))
    d = r["data"]
    if isinstance(d, list):
        return bytes(d)
    return bytes.fromhex(d)

def io(off, n):
    r = cmd({"cmd": "read_io", "addr": 0x04000000 + off, "len": n})
    d = r.get("data")
    if isinstance(d, list):
        return bytes(d)
    return bytes.fromhex(d) if d else r

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "mem":
        b = mem(int(a[1], 0), int(a[2], 0))
        base = int(a[1], 0)
        for i in range(0, len(b), 16):
            print("%08x  %s" % (base + i, b[i:i+16].hex(" ")))
    elif a[0] in ("u32", "u16", "u8"):
        n = {"u32": 4, "u16": 2, "u8": 1}[a[0]]
        print(hex(int.from_bytes(mem(int(a[1], 0), n), "little")))
    elif a[0] == "io":
        b = io(int(a[1], 0), int(a[2], 0))
        print(b.hex(" ") if isinstance(b, bytes) else b)
    elif a[0] == "raw":
        print(json.dumps(cmd(json.loads(a[1])))[:4000])
    elif a[0] == "save":
        print(cmd({"cmd": "savestate_save", "path": a[1]}))
    elif a[0] == "load":
        print(cmd({"cmd": "savestate_load", "path": a[1]}))
