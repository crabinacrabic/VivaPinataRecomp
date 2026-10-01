#!/usr/bin/env python3
"""find_code_gaps.py - find guest code that no recompiled function covers.

Code reached only through a function pointer the codegen scanner did not see
(virtual methods, callbacks, comparators, adjustor thunks) is left out of the
function table. The game then dies the first time it calls it:

    [FATAL] Call to invalid or unregistered function at guest address 0x...

This tool finds such code before the game hits it:
  1. the end of every function in generated/default (its last loc_ label plus
     the instructions after it, so inline switch tables are skipped over);
  2. the gaps between one function's end and the next function's start;
  3. the bytes of each gap, read from the guest memory of a RUNNING game
     (the code section is decompressed there; nothing is read from disk);
  4. gaps that hold code (a return or a branch) become `0xADDR = {}` lines for
     config/vivapinata_functions.toml. Zero padding, switch tables and SEH
     scope records (handler pointer + data) are skipped.

It generalises find_thunk_holes.py (8-byte adjustor thunks only).

Usage:  python tools/find_code_gaps.py [--pid PID]
        (default: the running vivapinata.exe; start the game first)
Exit 0 always; the TOML lines are on stdout, the summary on stderr.
"""

from __future__ import annotations

import argparse
import ctypes
import glob
import re
import struct
import subprocess
import sys
from ctypes import wintypes
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "default"
CONFIG = ROOT / "config" / "vivapinata_functions.toml"

GUEST_BASE = 0x100000000          # SDK: guest virtual memory arena
CODE_START, CODE_END = 0x820E0000, 0x826DFBB8   # log: "Function table initialized ... code=..."
# __savegprlr / __restgprlr / __savefpr / vmx save helpers: handled by the SDK, not functions.
SKIP_RANGES = [(0x8269A000, 0x8269B000)]

FN_RE = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)")
LOC_RE = re.compile(r"^loc_([0-9A-F]{8}):")
INS_RE = re.compile(r"^\t// [a-z]")


def function_ends() -> dict[int, int]:
    ends: dict[int, int] = {}
    for f in glob.glob(str(GEN / "vivapinata_recomp.*.cpp")):
        cur, anchor, n = None, 0, 0
        for line in open(f, encoding="utf-8", errors="replace"):
            m = FN_RE.match(line)
            if m:
                if cur is not None:
                    ends[cur] = anchor + 4 * n
                cur = int(m.group(1), 16)
                anchor, n = cur, 0
            elif cur is None:
                continue
            elif m := LOC_RE.match(line):
                a = int(m.group(1), 16)
                if a >= anchor:
                    anchor, n = a, 0
            elif INS_RE.match(line):
                n += 1
            elif line.startswith("}"):
                ends[cur] = anchor + 4 * n
                cur = None
        if cur is not None:
            ends[cur] = anchor + 4 * n
    return ends


def find_pid() -> int | None:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq vivapinata.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == "vivapinata.exe":
            return int(parts[1])
    return None


def read_code(pid: int) -> bytes:
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                      ctypes.POINTER(ctypes.c_size_t)]
    handle = k32.OpenProcess(0x0010 | 0x0400, False, pid)  # PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
    if not handle:
        sys.exit(f"cannot open process {pid} (error {ctypes.get_last_error()})")
    size = CODE_END - CODE_START
    buf = ctypes.create_string_buffer(size)
    done = ctypes.c_size_t(0)
    if not k32.ReadProcessMemory(handle, ctypes.c_void_p(GUEST_BASE + CODE_START), buf, size, ctypes.byref(done)) \
            or done.value != size:
        sys.exit(f"cannot read guest code (error {ctypes.get_last_error()}); is the game past loading?")
    k32.CloseHandle(handle)
    return buf.raw


MFLR_R12 = 0x7D8802A6           # standard prologue: mflr r12


def is_return(w: int) -> bool:   # bclr / bcctr without link (blr, beqlr, bctr, ...)
    return (w >> 26) == 19 and ((w >> 1) & 0x3FF) in (16, 528) and not (w & 1)


def is_end(w: int) -> bool:      # unconditional blr / bctr (BO = branch always) or b
    return (is_return(w) and ((w >> 21) & 0x14) == 0x14) or is_branch(w)


def is_branch(w: int) -> bool:   # b (no link)
    return (w >> 26) == 18 and not (w & 1)


def is_thunk(w0: int, w1: int) -> bool:   # addi r3,r3,N ; b target
    return (w0 >> 16) == 0x3863 and is_branch(w1)


def branch_target(a: int, w: int) -> int | None:
    """Target of a relative b / bc at address a."""
    op = w >> 26
    if op == 18 and not (w & 2):
        off = w & 0x03FFFFFC
        return a + (off - 0x04000000 if off & 0x02000000 else off)
    if op == 16 and not (w & 2):
        off = w & 0xFFFC
        return a + (off - 0x10000 if off & 0x8000 else off)
    return None


def split_functions(gap: list[tuple[int, int]], handlers: set[int]) -> list[tuple[int, int, int]] | None:
    """The functions in one gap, as (start, size, first word); None if it holds no code.

    Packed small functions (empty virtuals, thunks, call forwarders) are split:
    a function ends at blr / bctr / b once no branch inside it reaches further.
    """
    lo, hi = gap[0][0], gap[-1][0] + 4
    funcs = []
    k, n = 0, len(gap)
    while k < n:
        a, w = gap[k]
        if w == 0:
            k += 1
            continue
        if w in handlers and k + 1 < n:      # SEH scope record {handler, scope data}
            k += 2
            continue
        if (w >> 26) in (0, 1):              # not an instruction: import stubs (patched by the loader), data
            break
        start, reach, ended = k, a, False
        while k < n:
            a, w = gap[k]
            t = branch_target(a, w)
            if t is not None and not (w & 1) and lo <= t < hi:
                reach = max(reach, t)
            k += 1
            if is_end(w) and reach <= a:
                ended = True
                break
        if not ended:
            # A full prologue with no return: the function ends in a call that
            # does not come back (abort, throw); take the rest of the gap.
            if gap[start][1] != MFLR_R12:
                break
            k = n
        funcs.append((gap[start][0], gap[k - 1][0] + 4 - gap[start][0], gap[start][1]))
    return funcs or None


def known_functions() -> set[int]:
    if not CONFIG.exists():
        return set()
    return {int(m.group(1), 16) for m in re.finditer(r"^0x([0-9A-Fa-f]{8})\s*=", CONFIG.read_text(), re.M)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pid", type=int, help="vivapinata.exe process id (default: find it)")
    args = ap.parse_args()
    pid = args.pid or find_pid()
    if not pid:
        sys.exit("vivapinata.exe is not running: start the game (any screen after loading) and run again")

    ends = function_ends()
    code = read_code(pid)

    def word(a: int) -> int:
        return struct.unpack_from(">I", code, a - CODE_START)[0]

    starts = sorted(ends)
    known = known_functions()
    gaps = []
    for i, s in enumerate(starts):
        gap, nxt = ends[s], (starts[i + 1] if i + 1 < len(starts) else CODE_END)
        if nxt > gap and not any(lo <= gap < hi for lo, hi in SKIP_RANGES):
            addrs = list(range(gap, nxt, 4))
            gaps.append((s, list(zip(addrs, (word(a) for a in addrs)))))

    # SEH scope records sit in front of their function: {handler, scope data}.
    # Handlers are a few CRT routines, so their address leads many gaps; an
    # instruction word does not repeat like that.
    lead = Counter(next((w for _, w in g if w), 0) for _, g in gaps)
    handlers = {w for w, n in lead.items() if w and n >= 10 and CODE_START <= w < CODE_END}

    found: list[tuple[int, str]] = []
    stats = {"zero": 0, "table/SEH/import data": 0, "code": 0}
    for s, g in gaps:
        funcs = split_functions(g, handlers)
        if funcs is None:
            stats["zero" if not any(w for _, w in g) else "table/SEH/import data"] += 1
            continue
        stats["code"] += 1
        for start, size, first in funcs:
            if size == 8 and is_thunk(first, word(start + 4)):
                n = struct.unpack(">h", struct.pack(">H", first & 0xFFFF))[0]
                found.append((start, f"addi r3,r3,{n}; b - adjustor thunk after sub_{s:08X}"))
            else:
                found.append((start, f"{size} bytes after sub_{s:08X}, first word {first:08X}"))

    new = [(a, why) for a, why in found if a not in known]
    print(f"pid {pid}: {len(starts)} functions; gaps: {stats}; missing functions: {len(found)}, "
          f"not yet in {CONFIG.name}: {len(new)}", file=sys.stderr)
    for a, why in new:
        print(f"0x{a:08X} = {{}}   # {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
