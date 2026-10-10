#!/usr/bin/env python3
# Breakpoint tracer: catch the FIRST entry into the recompiled BIOS _start
# (GBA PC=0), main, firered_ctor_probe, and __do_global_ctors, in order, each
# with a shallow clean stack. On each hit, RBP-walk + raw-scan the (shallow)
# stack to show the real caller chain. Definitively answers "who runs the BIOS
# before main, and in what order relative to the CRT constructor machinery."
import ctypes as C, ctypes.wintypes as w, struct, sys, bisect

EXE  = r"F:\Projects\gbarecomp\FireRedRecomp\build\FireRedRecomp.exe"
NM   = r"F:\Projects\gbarecomp\FireRedRecomp\fr_syms.txt"
PREF = 0x140000000
# (static VMA, label) breakpoints
BPS = {
    0x146ce1306: "_start (BIOS PC=0)",
    0x1400014cb: "main",
    0x14000146e: "firered_ctor_probe",
    0x146da8500: "__do_global_ctors",
}
STOP_AT = "_start (BIOS PC=0)"   # do the deep walk + terminate when this hits

syms = []
for line in open(NM, encoding="utf-8", errors="replace"):
    p = line.split()
    if len(p) >= 3 and len(p[1]) == 1 and p[1] in "tTwWdDbBrR":
        try: syms.append((int(p[0], 16) - PREF, p[2]))
        except ValueError: pass
syms.sort(); rvas = [s[0] for s in syms]
def resolve(rva):
    i = bisect.bisect_right(rvas, rva) - 1
    return f"{syms[i][1]}+0x{rva-rvas[i]:x}" if i >= 0 else f"rva+0x{rva:x}"

k = C.WinDLL("kernel32", use_last_error=True)
k.OpenThread.restype = w.HANDLE; k.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.GetThreadContext.argtypes = [w.HANDLE, C.c_void_p]
k.SetThreadContext.argtypes = [w.HANDLE, C.c_void_p]
k.ReadProcessMemory.argtypes  = [w.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
k.WriteProcessMemory.argtypes = [w.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
k.FlushInstructionCache.argtypes = [w.HANDLE, C.c_void_p, C.c_size_t]
k.WaitForDebugEvent.argtypes = [C.c_void_p, w.DWORD]
k.ContinueDebugEvent.argtypes = [w.DWORD, w.DWORD, w.DWORD]
k.TerminateProcess.argtypes = [w.HANDLE, w.UINT]

class SI(C.Structure):
    _fields_ = [("cb", w.DWORD)] + [(n, w.LPWSTR) for n in ("a","b","c")] + \
        [(n, w.DWORD) for n in ("dwX","dwY","dwXS","dwYS","dwXC","dwYC","dwFill","dwFlags")] + \
        [("wShow", w.WORD), ("cbR2", w.WORD), ("lpR2", C.c_void_p),
         ("hIn", w.HANDLE), ("hOut", w.HANDLE), ("hErr", w.HANDLE)]
class PI(C.Structure):
    _fields_ = [("hProcess", w.HANDLE), ("hThread", w.HANDLE), ("dwPid", w.DWORD), ("dwTid", w.DWORD)]

si = SI(); si.cb = C.sizeof(si); pi = PI()
cmd = C.create_unicode_buffer('"%s" --help' % EXE)
if not k.CreateProcessW(EXE, cmd, None, None, False, 0x2 | 0x08000000, None, None, C.byref(si), C.byref(pi)):
    print("CreateProcess failed", C.get_last_error()); sys.exit(1)
hProc = pi.hProcess

def rpm(addr, n):
    b = (C.c_char * n)(); got = C.c_size_t(0)
    k.ReadProcessMemory(hProc, C.c_void_p(addr), b, n, C.byref(got)); return b.raw[:got.value]
def wpm(addr, data):
    got = C.c_size_t(0)
    k.WriteProcessMemory(hProc, C.c_void_p(addr), data, len(data), C.byref(got))
    k.FlushInstructionCache(hProc, C.c_void_p(addr), len(data))

exe_base = PREF; installed = {}   # loaded_addr -> orig_byte
DE = (C.c_byte * 256)()
hit_count = 0

def ctx_get(tid):
    raw = (C.c_byte * (1232 + 16))(); base = C.addressof(raw); aln = (base + 15) & ~15; off = aln - base
    struct.pack_into("<I", raw, off + 0x30, 0x10000B)  # CONTEXT_FULL
    hT = k.OpenThread(0x18, False, tid)  # GET|SET_CONTEXT
    k.GetThreadContext(hT, C.c_void_p(aln))
    g = lambda o: struct.unpack_from("<Q", raw, off + o)[0]
    return {"rsp": g(0x98), "rbp": g(0xA0), "rip": g(0xF8)}

def report_stack(c):
    rsp, rbp = c["rsp"], c["rbp"]
    # Chunked read upward (RPM is all-or-nothing per call; at a shallow first
    # entry rsp is near the stack base, so a big read overshoots and fails).
    blob = bytearray()
    for off in range(0, 0x20000, 0x200):
        chunk = rpm(rsp + off, 0x200)
        if len(chunk) != 0x200: blob += chunk; break
        blob += chunk
    blob = bytes(blob)
    print("  (read %d stack bytes above rsp)" % len(blob))
    lo, hi = rsp, rsp + len(blob)
    def att(v):
        b = exe_base
        return resolve(v - b) if b <= v < b + 0x8000000 else None
    print("  rsp=0x%x rbp=0x%x" % (rsp, rbp))
    # RBP walk (clean, shallow)
    print("  -- RBP frame-pointer walk (caller chain) --")
    cur = rbp; depth = 0
    while lo <= cur < hi - 16 and depth < 60:
        o = cur - rsp
        saved = struct.unpack_from("<Q", blob, o)[0]
        ret   = struct.unpack_from("<Q", blob, o + 8)[0]
        a = att(ret)
        print("    #%-3d %s" % (depth, (a if a else "0x%x (non-exe)" % ret)))
        if not (lo <= saved < hi) or saved <= cur: break
        cur = saved; depth += 1
    # Raw scan fallback (any exe code pointer on the shallow stack, in order)
    print("  -- raw exe code-pointers on shallow stack (first 40) --")
    seen = 0
    for i in range(0, len(blob) - 8, 8):
        v = struct.unpack_from("<Q", blob, i)[0]
        a = att(v)
        if a:
            print("    0x%x -> %s" % (rsp + i, a)); seen += 1
            if seen >= 40: break

while k.WaitForDebugEvent(C.byref(DE), 0xFFFFFFFF):
    code, pid, tid = struct.unpack_from("<III", DE, 0)
    cont = 0x00010002  # DBG_CONTINUE
    if code == 3:   # CREATE_PROCESS
        exe_base = struct.unpack_from("<Q", DE, 40)[0]
        print("exe_base=0x%x  installing %d breakpoints" % (exe_base, len(BPS)))
        for vma, lbl in BPS.items():
            la = exe_base + (vma - PREF)
            orig = rpm(la, 1)
            if orig:
                installed[la] = orig; wpm(la, b"\xCC")
    elif code == 1:  # EXCEPTION
        exc  = struct.unpack_from("<I", DE, 16)[0]
        eadr = struct.unpack_from("<Q", DE, 16 + 16)[0]   # ExceptionAddress
        if exc == 0x80000003 and eadr in installed:       # our INT3
            vma = eadr - exe_base + PREF
            lbl = BPS.get(vma, "?")
            hit_count += 1
            print("\n### BREAKPOINT HIT #%d: %s  (VMA 0x%x)" % (hit_count, lbl, vma))
            c = ctx_get(tid)
            report_stack(c)
            if lbl == STOP_AT or hit_count >= 12:
                print("\n(stopping)"); k.TerminateProcess(hProc, 0); break
            # restore byte, rewind rip, single-step, then reinstall
            wpm(eadr, installed[eadr])
            raw = (C.c_byte * (1232 + 16))(); base = C.addressof(raw); aln = (base + 15) & ~15; off = aln - base
            struct.pack_into("<I", raw, off + 0x30, 0x10000B)
            hT = k.OpenThread(0x18, False, tid)
            k.GetThreadContext(hT, C.c_void_p(aln))
            struct.pack_into("<Q", raw, off + 0xF8, eadr)              # rip = bp (rewind past int3)
            ef = struct.unpack_from("<I", raw, off + 0x44)[0] | 0x100  # set TF
            struct.pack_into("<I", raw, off + 0x44, ef)
            k.SetThreadContext(hT, C.c_void_p(aln))
            pending_reinstall = eadr
            k.ContinueDebugEvent(pid, tid, cont)
            # next event should be single-step
            k.WaitForDebugEvent(C.byref(DE), 0xFFFFFFFF)
            code2, pid2, tid2 = struct.unpack_from("<III", DE, 0)
            wpm(pending_reinstall, b"\xCC")   # reinstall
            k.ContinueDebugEvent(pid2, tid2, 0x00010002)
            continue
        elif exc == 0x80000003:
            cont = 0x00010002   # initial loader breakpoint -> swallow
        else:
            first = struct.unpack_from("<I", DE, 16 + 152)[0]
            cont = 0x80010001 if not first else 0x00010002
    elif code == 5:  # EXIT_PROCESS
        print("process exited before STOP_AT"); break
    k.ContinueDebugEvent(pid, tid, cont)
print("done")
