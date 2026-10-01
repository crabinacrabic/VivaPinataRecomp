#!/usr/bin/env python3
"""sample_profile.py - sampling CPU profiler for a running vivapinata.exe.

Every few milliseconds it suspends the busy threads of the game, reads their
instruction pointer and resumes them; DbgHelp turns the addresses into names
with the PDB next to the exe, so recompiled guest code shows up as
__imp__sub_XXXXXXXX (= guest address). When a sample lands outside the exe
(SDK runtime, GPU plugin, Windows), the nearest return address into the exe
found on the stack names the guest function that called it.

Needs nothing but Python on Windows; run it while the game is in the state
you want to measure (no debugger attached: it slows the game down).

Usage:  python tools/sample_profile.py [--pid PID] [--seconds 10] [--interval-ms 2] [--top 25]
"""

from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
import time
from collections import Counter, defaultdict
from ctypes import wintypes

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

TH32CS_SNAPTHREAD = 0x4
THREAD_ACCESS = 0x0002 | 0x0008 | 0x0040  # SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION
PROCESS_ACCESS = 0x0010 | 0x0400          # VM_READ | QUERY_INFORMATION
CONTEXT_CONTROL = 0x00100001
CONTEXT_INTEGER = 0x00100002
CONTEXT_SIZE = 0x4D0
OFF_FLAGS, OFF_RSP, OFF_RIP = 0x30, 0x98, 0xF8


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ThreadID", wintypes.DWORD),
                ("th32OwnerProcessID", wintypes.DWORD), ("tpBasePri", wintypes.LONG),
                ("tpDeltaPri", wintypes.LONG), ("dwFlags", wintypes.DWORD)]


class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wintypes.DWORD), ("EntryPoint", ctypes.c_void_p)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wintypes.ULONG), ("TypeIndex", wintypes.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wintypes.ULONG), ("Size", wintypes.ULONG), ("ModBase", ctypes.c_uint64),
                ("Flags", wintypes.ULONG), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64),
                ("Register", wintypes.ULONG), ("Scope", wintypes.ULONG), ("Tag", wintypes.ULONG),
                ("NameLen", wintypes.ULONG), ("MaxNameLen", wintypes.ULONG), ("Name", ctypes.c_char * 512)]


for fn, res, args in [
    (k32.OpenProcess, wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]),
    (k32.OpenThread, wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]),
    (k32.CreateToolhelp32Snapshot, wintypes.HANDLE, [wintypes.DWORD, wintypes.DWORD]),
    (k32.Thread32First, wintypes.BOOL, [wintypes.HANDLE, ctypes.POINTER(THREADENTRY32)]),
    (k32.Thread32Next, wintypes.BOOL, [wintypes.HANDLE, ctypes.POINTER(THREADENTRY32)]),
    (k32.K32GetModuleFileNameExA, wintypes.DWORD, [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_char_p,
                                                   wintypes.DWORD]),
    (k32.CloseHandle, wintypes.BOOL, [wintypes.HANDLE]),
    (dbghelp.SymSetOptions, wintypes.DWORD, [wintypes.DWORD]),
    (k32.SuspendThread, wintypes.DWORD, [wintypes.HANDLE]),
    (k32.ResumeThread, wintypes.DWORD, [wintypes.HANDLE]),
    (k32.GetThreadContext, wintypes.BOOL, [wintypes.HANDLE, ctypes.c_void_p]),
    (k32.GetThreadTimes, wintypes.BOOL, [wintypes.HANDLE] + [ctypes.POINTER(ctypes.c_uint64)] * 4),
    (k32.ReadProcessMemory, wintypes.BOOL, [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                            ctypes.POINTER(ctypes.c_size_t)]),
    (dbghelp.SymInitialize, wintypes.BOOL, [wintypes.HANDLE, ctypes.c_char_p, wintypes.BOOL]),
    (dbghelp.SymFromAddr, wintypes.BOOL, [wintypes.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
                                         ctypes.POINTER(SYMBOL_INFO)]),
    (dbghelp.SymGetModuleBase64, ctypes.c_uint64, [wintypes.HANDLE, ctypes.c_uint64]),
    (psapi.EnumProcessModules, wintypes.BOOL, [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD,
                                               ctypes.POINTER(wintypes.DWORD)]),
    (psapi.GetModuleInformation, wintypes.BOOL, [wintypes.HANDLE, wintypes.HMODULE, ctypes.POINTER(MODULEINFO),
                                                 wintypes.DWORD]),
    (psapi.GetModuleBaseNameA, wintypes.DWORD, [wintypes.HANDLE, wintypes.HMODULE, ctypes.c_char_p, wintypes.DWORD]),
]:
    fn.restype, fn.argtypes = res, args


def find_pid() -> int | None:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq vivapinata.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == "vivapinata.exe":
            return int(parts[1])
    return None


def threads_of(pid: int) -> list[int]:
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    tids = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            tids.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return tids


def cpu_time(h) -> int:
    c, e, kt, ut = (ctypes.c_uint64() for _ in range(4))
    k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut))
    return kt.value + ut.value  # 100 ns units


def modules(hproc) -> list[tuple[int, int, str]]:
    arr = (wintypes.HMODULE * 1024)()
    need = wintypes.DWORD()
    psapi.EnumProcessModules(hproc, arr, ctypes.sizeof(arr), ctypes.byref(need))
    out = []
    for i in range(need.value // ctypes.sizeof(wintypes.HMODULE)):
        mi = MODULEINFO()
        name = ctypes.create_string_buffer(260)
        psapi.GetModuleInformation(hproc, arr[i], ctypes.byref(mi), ctypes.sizeof(mi))
        psapi.GetModuleBaseNameA(hproc, arr[i], name, 260)
        out.append((mi.lpBaseOfDll or 0, (mi.lpBaseOfDll or 0) + mi.SizeOfImage, name.value.decode(errors="replace")))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pid", type=int)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--interval-ms", type=float, default=2.0)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--min-cpu", type=float, default=10.0, help="sample threads above this CPU %% (default 10)")
    args = ap.parse_args()
    pid = args.pid or find_pid()
    if not pid:
        sys.exit("vivapinata.exe is not running")
    hproc = k32.OpenProcess(PROCESS_ACCESS, False, pid)

    # Busy threads: CPU time over one second.
    handles = {t: k32.OpenThread(THREAD_ACCESS, False, t) for t in threads_of(pid)}
    handles = {t: h for t, h in handles.items() if h}
    before = {t: cpu_time(h) for t, h in handles.items()}
    time.sleep(1.0)
    load = {t: (cpu_time(h) - before[t]) / 1e5 for t, h in handles.items()}  # % of one core
    busy = [t for t, pct in sorted(load.items(), key=lambda x: -x[1]) if pct >= args.min_cpu]
    print(f"pid {pid}: busy threads " + ", ".join(f"{t} ({load[t]:.0f}%)" for t in busy))

    mods = modules(hproc)
    exe = next((m for m in mods if m[2].lower() == "vivapinata.exe"), None)
    # Project code: the exe and the SDK DLLs. A sample in Windows / CRT code is
    # credited to the nearest project return address on the stack.
    ours = [m for m in mods if m[2].lower() == "vivapinata.exe" or m[2].lower().startswith("rex")]

    def in_ours(v: int) -> bool:
        return any(lo <= v < hi for lo, hi, _ in ours)
    ctx_buf = ctypes.create_string_buffer(CONTEXT_SIZE + 16)
    ctx_addr = (ctypes.addressof(ctx_buf) + 15) & ~15
    stack = ctypes.create_string_buffer(8192)
    raw: dict[int, Counter] = {t: Counter() for t in busy}       # rip -> count
    caller: dict[int, Counter] = {t: Counter() for t in busy}    # nearest project return address
    n_samples = 0
    deadline = time.perf_counter() + args.seconds
    while time.perf_counter() < deadline:
        for t in busy:
            h = handles[t]
            if k32.SuspendThread(h) == 0xFFFFFFFF:
                continue
            ctypes.c_uint32.from_address(ctx_addr + OFF_FLAGS).value = CONTEXT_CONTROL
            if k32.GetThreadContext(h, ctx_addr):
                rip = ctypes.c_uint64.from_address(ctx_addr + OFF_RIP).value
                rsp = ctypes.c_uint64.from_address(ctx_addr + OFF_RSP).value
                raw[t][rip] += 1
                if not in_ours(rip):
                    got = ctypes.c_size_t()
                    if k32.ReadProcessMemory(hproc, ctypes.c_void_p(rsp), stack, 8192, ctypes.byref(got)):
                        vals = (ctypes.c_uint64 * (got.value // 8)).from_buffer_copy(stack.raw[:got.value // 8 * 8])
                        ret = next((v for v in vals if in_ours(v)), 0)
                        caller[t][ret] += 1
            k32.ResumeThread(h)
        n_samples += 1
        time.sleep(args.interval_ms / 1000.0)

    # Symbols.
    exe_dir = None
    if exe:
        name = ctypes.create_string_buffer(1024)
        k32.K32GetModuleFileNameExA(hproc, ctypes.c_void_p(exe[0]), name, 1024)
        exe_dir = name.value.rsplit(b"\\", 1)[0]
    dbghelp.SymSetOptions(0x2 | 0x4)  # UNDNAME | DEFERRED_LOADS
    dbghelp.SymInitialize(hproc, exe_dir, True)
    cache: dict[int, str] = {}

    def sym(addr: int) -> str:
        if addr == 0:
            return "?"
        if addr in cache:
            return cache[addr]
        info = SYMBOL_INFO()
        info.SizeOfStruct = 88  # sizeof(SYMBOL_INFO) with a 1-char name
        info.MaxNameLen = 511
        disp = ctypes.c_uint64()
        mod = next((m[2] for m in mods if m[0] <= addr < m[1]), "?")
        if dbghelp.SymFromAddr(hproc, addr, ctypes.byref(disp), ctypes.byref(info)):
            s = f"{mod}!{info.Name.decode(errors='replace')}"
        else:
            base = next((m[0] for m in mods if m[0] <= addr < m[1]), 0)
            s = f"{mod}+0x{addr - base:X}"
        cache[addr] = s
        return s

    print(f"{n_samples} sampling rounds over {args.seconds:.0f} s\n")
    for t in busy:
        total = sum(raw[t].values())
        if not total:
            continue
        by_fn: Counter = Counter()
        by_mod: Counter = Counter()
        for rip, c in raw[t].items():
            s = sym(rip)
            by_fn[s] += c
            by_mod[s.split("!")[0].split("+")[0]] += c
        print(f"=== thread {t}: {load[t]:.0f}% CPU, {total} samples; by module: " +
              ", ".join(f"{m} {100 * c / total:.0f}%" for m, c in by_mod.most_common(5)))
        for s, c in by_fn.most_common(args.top):
            print(f"  {100 * c / total:5.1f}%  {s}")
        if caller[t]:
            by_caller: Counter = Counter()
            for ret, c in caller[t].items():
                by_caller[sym(ret)] += c
            print("  in Windows/CRT code, called from (nearest exe/SDK return address on the stack):")
            for s, c in by_caller.most_common(10):
                print(f"  {100 * c / total:5.1f}%  {s}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
