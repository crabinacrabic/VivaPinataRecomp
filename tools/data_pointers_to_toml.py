#!/usr/bin/env python3
"""data_pointers_to_toml.py - undiscovered vtable methods from the data-pointer scan.

Reads <exe_dir>/logs/data_pointers.txt written by
debug_tools::PerformDataPointerScan (dev_debug_runtime = true): every
big-endian word in the image's data sections that points into the code
range, with the length of the run of consecutive code pointers it sits in
(a vtable is such a run) and whether the dispatcher knew the target.

An UNKNOWN target is kept as a candidate when
  * it is not the start of a function codegen already emitted, and
  * it does not fall inside the body of a known function (start + 4 * emitted
    instructions; jump-table data at the end of a function can slip through,
    so a candidate a few bytes past a function end deserves a look), and
  * its run is at least --min-run entries long (default 2; random data
    almost never yields two adjacent code pointers).

Usage:  python tools/data_pointers_to_toml.py [path/to/data_pointers.txt]
                                              [--min-run N] [--toml]
"""

from __future__ import annotations

import glob
import re
import sys
from bisect import bisect_right
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "default"

FN_RE = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]{8})\) \{")
INS_RE = re.compile(r"^\s*// [a-z][a-z0-9.]*")
PTR_RE = re.compile(r"\[ptr\] at=0x([0-9A-Fa-f]{8}) value=0x([0-9A-Fa-f]{8}) run=(\d+) idx=(\d+) (known|UNKNOWN)")


def function_spans() -> list[tuple[int, int]]:
    spans: list[tuple[int, int]] = []
    for f in glob.glob(str(GEN / "vivapinata_recomp.*.cpp")):
        cur: int | None = None
        n = 0
        with open(f, encoding="utf-8", errors="replace") as fp:
            for line in fp:
                m = FN_RE.match(line)
                if m:
                    if cur is not None:
                        spans.append((cur, cur + 4 * n))
                    cur = int(m.group(1)[4:], 16)
                    n = 0
                    continue
                if cur is None:
                    continue
                if line.startswith("}"):
                    spans.append((cur, cur + 4 * n))
                    cur = None
                    continue
                if INS_RE.match(line):
                    n += 1
        if cur is not None:
            spans.append((cur, cur + 4 * n))
    spans.sort()
    return spans


def newest_log() -> Path | None:
    cands = [Path(d) / "logs" / "data_pointers.txt" for d in glob.glob(str(ROOT / "out" / "build" / "*"))]
    cands = [c for c in cands if c.exists()]
    return max(cands, key=lambda c: c.stat().st_mtime) if cands else None


def main(argv: list[str]) -> int:
    as_toml = "--toml" in argv
    min_run = 2
    if "--min-run" in argv:
        min_run = int(argv[argv.index("--min-run") + 1])
    args = [a for i, a in enumerate(argv) if not a.startswith("--") and (i == 0 or argv[i - 1] != "--min-run")]
    log = Path(args[0]) if args else newest_log()
    if log is None or not log.exists():
        print("data_pointers.txt not found - run with dev_debug_runtime = true first", file=sys.stderr)
        return 1

    # target -> (best run length, list of vtable slots referencing it)
    targets: dict[int, tuple[int, list[tuple[int, int]]]] = {}
    total = 0
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        m = PTR_RE.search(line)
        if not m:
            continue
        total += 1
        if m.group(5) != "UNKNOWN":
            continue
        at, val, run, idx = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)), int(m.group(4))
        best, refs = targets.get(val, (0, []))
        targets[val] = (max(best, run), refs + [(at, idx)])

    spans = function_spans()
    starts = [s for s, _ in spans]
    new: list[tuple[int, int, list[tuple[int, int]]]] = []
    mid: list[tuple[int, int]] = []
    weak: list[tuple[int, int]] = []
    old = 0
    for val in sorted(targets):
        run, refs = targets[val]
        i = bisect_right(starts, val) - 1
        if i >= 0 and starts[i] == val:
            old += 1
        elif i >= 0 and val < spans[i][1]:
            mid.append((val, starts[i]))
        elif run < min_run:
            weak.append((val, run))
        else:
            new.append((val, run, refs))

    if as_toml:
        for val, run, refs in new:
            print(f"0x{val:08X} = {{}}   # vtable @0x{refs[0][0]:08X} slot {refs[0][1]} (run {run})")
        return 0

    print(f"log: {log}")
    print(f"code pointers: {total}  unknown targets: {len(targets)}  -> NEW {len(new)}  "
          f"MID {len(mid)}  weak(run<{min_run}) {len(weak)}  already-known {old}")
    for val, run, refs in new:
        i = bisect_right(starts, val) - 1
        prev_end = spans[i][1] if i >= 0 else 0
        nxt = starts[i + 1] if i + 1 < len(starts) else 0
        print(f"  NEW  0x{val:08X}  run={run:<3} refs={len(refs):<2} first@0x{refs[0][0]:08X}[{refs[0][1]}]  "
              f"gap 0x{prev_end:08X}..0x{nxt:08X}")
    for val, s in mid[:40]:
        print(f"  MID  0x{val:08X}  inside sub_{s:08X}  (jump table / boundary?)")
    if len(mid) > 40:
        print(f"  ... {len(mid) - 40} more MID")
    for val, run in weak[:20]:
        print(f"  WEAK 0x{val:08X}  run={run}")
    if len(weak) > 20:
        print(f"  ... {len(weak) - 20} more WEAK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
