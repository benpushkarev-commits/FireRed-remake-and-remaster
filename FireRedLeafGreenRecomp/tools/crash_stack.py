#!/usr/bin/env python3
# No-install Win32 debugger for the FireRedRecomp startup STACK_OVERFLOW.
#
# Launches FireRedRecomp.exe under DEBUG, catches the STACK_OVERFLOW
# (0xC00000FD) first-chance, walks the *entire committed* stack (found via
# VirtualQueryEx, not a fixed 16 MB guess), and attributes every return
# address to its loaded module (Toolhelp snapshot). EXE addresses resolve to
# nm symbols. Prints:
#   - the faulting RIP (the leaf of the recursion),
#   - a histogram of the dominant recursing return-addresses,
#   - the call chain in stack order: innermost frames near %rsp AND the
#     oldest frames near the stack base (what *initiated* the recursion).
#
# Usage: python crash_stack.py [args-to-pass-to-exe...]   (default: --help)
import ctypes as C, ctypes.wintypes as w, struct, sys, bisect

EXE  = r"F:\Projects\gbarecomp\FireRedRecomp\build\FireRedRecomp.exe"
NM   = r"F:\Projects\gbarecomp\FireRedRecomp\fr_syms.txt"   # "<vma> <type> <name>" @ 0x140000000
PREF = 0x140000000
EXTRA_ARGS = sys.argv[1:] if len(sys.argv) > 1 else ["--help"]
STATUS_STACK_OVERFLOW = 0xC00000FD

# ---- exe symbols (rva, name); keep code+data so attribution never goes blank ----
syms = []
for line in open(NM, encoding="utf-8", errors="replace"):
    p = line.split()
    if len(p) >= 3 and len(p[1]) == 1 and p[1] in "tTwWdDbBrR":
        try: syms.append((int(p[0], 16) - PREF, p[2]))
        except ValueError: pass
syms.sort()
rvas = [s[0] for s in syms]
def exe_resolve(rva):
    i = bisect.bisect_right(rvas, rva) - 1
    return f"{syms[i][1]}+0x{rva-rvas[i]:x}" if i >= 0 else f"rva+0x{rva:x}"

k = C.WinDLL("kernel32", use_last_error=True)
k.OpenThread.restype = w.HANDLE
k.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.GetThreadContext.argtypes = [w.HANDLE, C.c_void_p]
k.ReadProcessMemory.argtypes = [w.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
k.VirtualQueryEx.restype = C.c_size_t
k.VirtualQueryEx.argtypes = [w.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t]
k.WaitForDebugEvent.argtypes = [C.c_void_p, w.DWORD]
k.ContinueDebugEvent.argtypes = [w.DWORD, w.DWORD, w.DWORD]
k.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
k.CreateToolhelp32Snapshot.restype = w.HANDLE
k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
k.Module32FirstW.argtypes = [w.HANDLE, C.c_void_p]
k.Module32FirstW.restype = w.BOOL
k.Module32NextW.argtypes = [w.HANDLE, C.c_void_p]
k.Module32NextW.restype = w.BOOL
k.CloseHandle.argtypes = [w.HANDLE]
k.OpenThread.restype = w.HANDLE

class SI(C.Structure):
    _fields_ = [("cb", w.DWORD)] + [(n, w.LPWSTR) for n in ("a","b","c")] + \
        [(n, w.DWORD) for n in ("dwX","dwY","dwXS","dwYS","dwXC","dwYC","dwFill","dwFlags")] + \
        [("wShow", w.WORD), ("cbR2", w.WORD), ("lpR2", C.c_void_p),
         ("hIn", w.HANDLE), ("hOut", w.HANDLE), ("hErr", w.HANDLE)]
class PI(C.Structure):
    _fields_ = [("hProcess", w.HANDLE), ("hThread", w.HANDLE),
                ("dwPid", w.DWORD), ("dwTid", w.DWORD)]
class MBI(C.Structure):
    _fields_ = [("BaseAddress", C.c_void_p), ("AllocationBase", C.c_void_p),
                ("AllocationProtect", w.DWORD), ("__a1", w.DWORD),
                ("RegionSize", C.c_size_t), ("State", w.DWORD),
                ("Protect", w.DWORD), ("Type", w.DWORD), ("__a2", w.DWORD)]
class ME32(C.Structure):
    _fields_ = [("dwSize", w.DWORD), ("th32ModuleID", w.DWORD), ("th32ProcessID", w.DWORD),
                ("GlblcntUsage", w.DWORD), ("ProccntUsage", w.DWORD),
                ("modBaseAddr", C.c_void_p), ("modBaseSize", w.DWORD),
                ("hModule", C.c_void_p), ("szModule", C.c_wchar * 256),
                ("szExePath", C.c_wchar * 260)]

si = SI(); si.cb = C.sizeof(si); pi = PI()
DEBUG_ONLY_THIS_PROCESS = 0x2; CREATE_NO_WINDOW = 0x08000000
cmd = C.create_unicode_buffer('"%s" %s' % (EXE, " ".join(EXTRA_ARGS)))
if not k.CreateProcessW(EXE, cmd, None, None, False,
                        DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW,
                        None, None, C.byref(si), C.byref(pi)):
    print("CreateProcess failed", C.get_last_error()); sys.exit(1)
hProc = pi.hProcess; the_pid = pi.dwPid

def rpm(addr, n):
    buf = (C.c_char * n)(); got = C.c_size_t(0)
    k.ReadProcessMemory(hProc, C.c_void_p(addr), buf, n, C.byref(got))
    return buf.raw[:got.value]

def read_committed_stack(rsp):
    """Walk upward from rsp over contiguous committed regions to the stack
    base, reading each. Robust to guard pages / partial commits."""
    out = bytearray(); start = rsp; mbi = MBI()
    addr = rsp
    while True:
        if not k.VirtualQueryEx(hProc, C.c_void_p(addr), C.byref(mbi), C.sizeof(mbi)):
            break
        base = mbi.BaseAddress or 0; size = mbi.RegionSize
        MEM_COMMIT = 0x1000
        if mbi.State != MEM_COMMIT:
            break  # hit reserve/guard top -> end of live stack
        region_end = base + size
        rd_from = max(addr, base)
        out += rpm(rd_from, region_end - rd_from)
        addr = region_end
        if size == 0: break
    return start, bytes(out)

def snapshot_modules():
    TH32CS_SNAPMODULE = 0x8; TH32CS_SNAPMODULE32 = 0x10
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, the_pid)
    mods = []
    if snap and snap != C.c_void_p(-1).value:
        me = ME32(); me.dwSize = C.sizeof(me)
        if k.Module32FirstW(snap, C.byref(me)):
            while True:
                b = me.modBaseAddr or 0
                mods.append((b, b + me.modBaseSize, me.szModule))
                me2 = ME32(); me2.dwSize = C.sizeof(me2)
                if not k.Module32NextW(snap, C.byref(me2)): break
                me = me2
        k.CloseHandle(snap)
    mods.sort()
    return mods

MODS = []
def attribute(addr):
    for base, end, name in MODS:
        if base <= addr < end:
            if name.lower().startswith("firered"):
                return "EXE", exe_resolve(addr - base)
            return name, "+0x%x" % (addr - base)
    return None, None

DE = (C.c_byte * 256)()
exe_base = PREF
while k.WaitForDebugEvent(C.byref(DE), 0xFFFFFFFF):
    code, pid, tid = struct.unpack_from("<III", DE, 0)
    cont = 0x00010002  # DBG_CONTINUE
    if code == 3:      # CREATE_PROCESS_DEBUG_EVENT -> lpBaseOfImage at +40
        exe_base = struct.unpack_from("<Q", DE, 40)[0]
    elif code == 1:    # EXCEPTION_DEBUG_EVENT
        exc   = struct.unpack_from("<I", DE, 16)[0]
        first = struct.unpack_from("<I", DE, 16 + 152)[0]
        if exc == STATUS_STACK_OVERFLOW:
            MODS = snapshot_modules()
            raw = (C.c_byte * (1232 + 16))()
            base = C.addressof(raw); aln = (base + 15) & ~15; off = aln - base
            struct.pack_into("<I", raw, off + 0x30, 0x10000B)  # CONTEXT_FULL
            hT = k.OpenThread(0x8, False, tid)
            if not k.GetThreadContext(hT, C.c_void_p(aln)):
                print("GetThreadContext failed", C.get_last_error())
            rsp = struct.unpack_from("<Q", raw, off + 0x98)[0]
            rbp = struct.unpack_from("<Q", raw, off + 0xA0)[0]
            rip = struct.unpack_from("<Q", raw, off + 0xF8)[0]
            rm, ro = attribute(rip)
            print("loaded modules: %d" % len(MODS))
            for b, e, n in MODS:
                if n.lower().startswith("firered") or n.lower() in ("ntdll.dll","kernel32.dll","kernelbase.dll","msvcrt.dll"):
                    print("   %-22s base=0x%012x size=0x%x" % (n, b, e - b))
            print("rsp=0x%x rip=0x%x rip=%s%s" % (rsp, rip, rm, ro))

            start, data = read_committed_stack(rsp)
            print("committed stack read: %d bytes (0x%x..0x%x)" % (len(data), start, start + len(data)))

            hist = {}; total = 0
            for i in range(0, len(data) - 8, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                m, o = attribute(v)
                if m is not None:
                    hist[v] = hist.get(v, 0) + 1; total += 1
            print("code-pointer qwords on stack: %d" % total)
            print("=== dominant recursing return-addresses (top 20) ===")
            for v, c in sorted(hist.items(), key=lambda kv: -kv[1])[:20]:
                m, o = attribute(v); print("  x%-7d %-14s %s%s  [0x%x]" % (c, m, m, o, v))

            def chain(byte_iter, label, limit=30):
                print("=== %s ===" % label)
                seen = 0
                for i in byte_iter:
                    v = struct.unpack_from("<Q", data, i)[0]
                    m, o = attribute(v)
                    if m is None: continue
                    print("  0x%012x -> %s%s" % (start + i, m, o))
                    seen += 1
                    if seen >= limit: break
            # Precise call chain via RBP frame-pointer walk over the dumped
            # stack. Recompiled frames keep RBP (-O0); the faulting leaf may be
            # an -O2 runtime helper with no frame pointer, so seed by scanning
            # the first 64 KB for the RBP chain that walks farthest (the real
            # one self-validates: monotonically increasing RBP, code returns).
            lo, hi = start, start + len(data)
            def is_code(v): return attribute(v)[0] is not None
            def walk_from(seed, cap):
                fr = []; cur = seed
                while lo <= cur < hi - 16 and len(fr) < cap:
                    o = cur - start
                    saved = struct.unpack_from("<Q", data, o)[0]
                    ret   = struct.unpack_from("<Q", data, o + 8)[0]
                    if not is_code(ret): break
                    fr.append((cur, ret))
                    if not (lo <= saved < hi) or saved <= cur: break
                    cur = saved
                return fr
            seed = None
            for probe in range(0, min(len(data) - 16, 0x10000), 8):
                cand = start + probe
                s2 = struct.unpack_from("<Q", data, probe)[0]
                if not (lo <= s2 < hi) or s2 <= cand: continue
                if len(walk_from(cand, 50)) > 30: seed = cand; break
            # Memory-light full walk to the true bottom: keep count + last 80.
            from collections import deque
            tail = deque(maxlen=80); head = []; cur = seed; n = 0
            while seed is not None and lo <= cur < hi - 16 and n < 50_000_000:
                o = cur - start
                saved = struct.unpack_from("<Q", data, o)[0]
                ret   = struct.unpack_from("<Q", data, o + 8)[0]
                if not is_code(ret): break
                if n < 24: head.append((cur, ret))
                tail.append((cur, ret)); n += 1
                if not (lo <= saved < hi) or saved <= cur: break
                cur = saved
            frames = head; frames_tail = list(tail); frames_total = n
            print("=== RBP-walked frames: %d total (true bottom reached) ===" % frames_total)
            def show(lst, base_idx, label):
                print("--- %s ---" % label)
                for j, (fa, ret) in enumerate(lst):
                    m, oo = attribute(ret)
                    s = ("%s%s" % (m, oo)) if m else ("0x%x (?)" % ret)
                    print("  #%-8d ret=%s" % (base_idx + j, s))
            show(frames, 0, "leaf (innermost) frames")
            show(frames_tail, frames_total - len(frames_tail),
                 "TRUE base (the pre-main initiator of the recursion)")

            k.TerminateProcess(hProc, 1); break
        cont = 0x80010001 if not first else 0x00010002
    elif code == 5:    # EXIT_PROCESS
        break
    k.ContinueDebugEvent(pid, tid, cont)
print("done")
