#!/usr/bin/env python3
"""find_thunk_holes.py - list MSVC adjustor thunks the codegen scanner missed.

MSVC emits `this`-adjustor thunks for multiple inheritance as packed 8-byte
stubs:

    addi r3,r3,-N      ; shift `this` to the primary base
    b    Method        ; tail-jump to the real implementation

They are referenced only from vtables, so when a vtable is not discovered the
thunk is not registered and a virtual call through it dies with
"Call to invalid or unregistered function at guest address 0x...".

This tool parses generated/default/vivapinata_recomp.*.cpp, collects every
function whose body is exactly `addi r3,r3,-N; b`, and reports each 8-byte gap
that is bracketed by such thunks (packed blocks never contain padding). Paste
the output into config/vivapinata_functions.toml as `0xADDR = {}`.

Usage:  python tools/find_thunk_holes.py [--toml]
Exit 0 always; the list is on stdout.
"""

from __future__ import annotations

import glob
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "default"

FN_RE = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]{8})\) \{")
INS_RE = re.compile(r"^\s*// ([a-z][a-z0-9.]*)\s*(.*)$")


def parse_bodies() -> dict[int, list[tuple[str, str]]]:
    bodies: dict[int, list[tuple[str, str]]] = {}
    for f in glob.glob(str(GEN / "vivapinata_recomp.*.cpp")):
        cur: int | None = None
        ins: list[tuple[str, str]] = []
        with open(f, encoding="utf-8", errors="replace") as fp:
            for line in fp:
                m = FN_RE.match(line)
                if m:
                    if cur is not None:
                        bodies[cur] = ins
                    cur = int(m.group(1)[4:], 16)
                    ins = []
                    continue
                if cur is None:
                    continue
                if line.startswith("}"):
                    bodies[cur] = ins
                    cur = None
                    continue
                m = INS_RE.match(line)
                if m:
                    ins.append((m.group(1), m.group(2)))
        if cur is not None:
            bodies[cur] = ins
    return bodies


def main(argv: list[str]) -> int:
    as_toml = "--toml" in argv
    bodies = parse_bodies()
    if not bodies:
        print("no generated code found - run codegen (F7) first", file=sys.stderr)
        return 1

    thunks: dict[int, int] = {}
    for a, ins in bodies.items():
        if (len(ins) == 2 and ins[0][0] == "addi" and ins[0][1].startswith("r3,r3,-")
                and ins[1][0] == "b"):
            thunks[a] = int(ins[1][1], 16)

    known = set(bodies)
    holes: list[int] = []
    for a in sorted(thunks):
        h = a + 8
        run: list[int] = []
        while h not in known and len(run) < 8:
            run.append(h)
            h += 8
        if h in thunks:          # gap closed by another thunk -> it is packed thunk space
            holes.extend(run)
    holes = sorted(set(holes))

    if as_toml:
        for h in holes:
            print(f"0x{h:08X} = {{}}")
    else:
        print(f"functions: {len(bodies)}  adjustor thunks: {len(thunks)}  missing: {len(holes)}")
        for h in holes:
            print(f"  0x{h:08X}   after sub_{h - 8:08X} -> 0x{thunks.get(h - 8, 0):08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
