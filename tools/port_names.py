#!/usr/bin/env python3
"""port_names.py - carry function names from Trouble in Paradise over to Viva Pinata.

Viva Pinata (2006) and Viva Pinata: Trouble in Paradise (2008) run on the same Rare
engine. TiP-Recomp (SolarCookies, used with the author's permission) named about 50
engine functions (config/retip_hooks.toml there: gardenMainGetGardenScene,
supportPinataCreateGeneralEx, ...); this project has none, and default.xex contains no
function-name strings.

The generated C++ of both projects keeps the PowerPC disassembly of every instruction as
a comment ("// mflr r12", "// bl 0x8269a048"), which gives each function's mnemonic
sequence and its call list. Matching, in the spirit of BinDiff:

  1. anchors: functions whose mnemonic sequence is identical and unique in both games
     (at least MIN_ANCHOR instructions);
  2. propagation over the call graph, round by round: for every matched pair, the call
     lists are aligned (difflib) and unmatched calls in the same position vote for each
     other; a pair is accepted when the vote is mutual and the code is similar enough
     (length ratio, mnemonic histogram). Callers are paired when each side has exactly
     one unmatched caller;
  3. named TiP functions that the graph did not reach get their most similar candidate
     by code alone (graded medium/low; verify by hand).

Reads only generated/default/*_recomp.*.cpp of both projects (never runs codegen).
Writes docs/NAMES_FROM_TIP.md (all named TiP functions) and src/vp_names.h (reliable
matches only).

Usage:
  python tools/port_names.py [--tip C:/Recompiles/VivaPinata_TroubleInParadise_xbox360]
"""

from __future__ import annotations

import argparse
import difflib
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TIP = ROOT.parent / "VivaPinata_TroubleInParadise_xbox360"
REPORT = ROOT / "docs" / "NAMES_FROM_TIP.md"
HEADER = ROOT / "src" / "vp_names.h"

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
INSN_RE = re.compile(r"^\t// ([a-z][a-z0-9.+-]*)(?:\s+(.*))?$")
ADDR_RE = re.compile(r"_([0-9A-Fa-f]{8})$")
CALL_RE = re.compile(r"0x([0-9a-f]{8})\b")

MIN_ANCHOR = 12      # instructions; shorter identical functions are too common to trust
MIN_COSINE = 0.80    # mnemonic histogram similarity required to accept a graph match
MAX_ROUNDS = 60


class Func:
    __slots__ = ("name", "addr", "insns", "calls", "callers", "hist", "norm")

    def __init__(self, name: str, addr: int):
        self.name, self.addr = name, addr
        self.insns: list[str] = []
        self.calls: list[int] = []      # call targets in order (bl, and b to another function)
        self.callers: set[int] = set()
        self.hist: Counter = Counter()
        self.norm = 0.0


def load(root: Path) -> dict[int, Func]:
    files = sorted((root / "generated" / "default").glob("*_recomp.*.cpp"))
    if not files:
        sys.exit(f"error: no generated code under {root / 'generated' / 'default'} (build the project once)")
    funcs: dict[int, Func] = {}
    for path in files:
        cur = None
        with path.open(encoding="utf-8", errors="replace") as f:
            for line in f:
                m = FUNC_RE.match(line)
                if m:
                    a = ADDR_RE.search(m.group(1))
                    cur = Func(m.group(1), int(a.group(1), 16)) if a else None
                    if cur:
                        funcs[cur.addr] = cur
                    continue
                if cur is None:
                    continue
                mm = INSN_RE.match(line)
                if not mm:
                    continue
                op, args = mm.group(1), mm.group(2) or ""
                cur.insns.append(op)
                if op in ("bl", "b") and (c := CALL_RE.search(args)):
                    cur.calls.append(int(c.group(1), 16))
    for fn in funcs.values():
        # a b inside the function is a jump, not a call
        fn.calls = [t for t in fn.calls if t in funcs and t != fn.addr]
        fn.hist = Counter(fn.insns)
        fn.norm = math.sqrt(sum(v * v for v in fn.hist.values()))
    for fn in funcs.values():
        for t in fn.calls:
            funcs[t].callers.add(fn.addr)
    return funcs


def cosine(a: Func, b: Func) -> float:
    if not a.norm or not b.norm:
        return 0.0
    return sum(v * b.hist[k] for k, v in a.hist.items()) / (a.norm * b.norm)


def similar(a: Func, b: Func) -> bool:
    la, lb = len(a.insns), len(b.insns)
    if not la or not lb or max(la, lb) > 2.0 * min(la, lb) + 4:
        return False
    return cosine(a, b) >= MIN_COSINE


def anchors(tip: dict[int, Func], vp: dict[int, Func]) -> dict[int, int]:
    """TiP address -> VP1 address for identical, unique, long enough functions."""
    def unique(funcs):
        c = Counter(tuple(f.insns) for f in funcs.values() if len(f.insns) >= MIN_ANCHOR)
        return {tuple(f.insns): f.addr for f in funcs.values()
                if len(f.insns) >= MIN_ANCHOR and c[tuple(f.insns)] == 1}
    t, v = unique(tip), unique(vp)
    return {t[s]: v[s] for s in t.keys() & v.keys()}


def propagate(tip: dict[int, Func], vp: dict[int, Func], matched: dict[int, int]) -> dict[int, tuple]:
    """Extends `matched` over the call graph. Returns evidence per new pair: (round, votes, cosine)."""
    inverse = {v: t for t, v in matched.items()}
    evidence: dict[int, tuple] = {}
    for rnd in range(1, MAX_ROUNDS + 1):
        votes: dict[int, Counter] = defaultdict(Counter)
        for t_addr, v_addr in matched.items():
            t, v = tip[t_addr], vp[v_addr]
            # callees in call order
            if t.calls and v.calls:
                a = [matched.get(x, ("T", x)) for x in t.calls]
                b = [y if y in inverse else ("V", y) for y in v.calls]
                sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
                for op, i1, i2, j1, j2 in sm.get_opcodes():
                    if op != "replace":
                        continue
                    xs = [x[1] for x in a[i1:i2] if isinstance(x, tuple)]
                    ys = [y[1] for y in b[j1:j2] if isinstance(y, tuple)]
                    if i2 - i1 == j2 - j1:
                        # same shape: pair by position
                        for x, y in zip(a[i1:i2], b[j1:j2]):
                            if isinstance(x, tuple) and isinstance(y, tuple):
                                votes[x[1]][y[1]] += 1
                    elif xs and ys and len(xs) * len(ys) <= 400:
                        # the code changed between the games: pair by the most similar code
                        for x in xs:
                            scored = sorted(((cosine(tip[x], vp[y]), y) for y in ys), reverse=True)
                            if scored[0][0] >= MIN_COSINE and (len(scored) == 1 or scored[0][0] - scored[1][0] >= 0.05):
                                votes[x][scored[0][1]] += 1
            # the only unmatched caller on each side
            ut = [c for c in t.callers if c not in matched]
            uv = [c for c in v.callers if c not in inverse]
            if len(ut) == 1 and len(uv) == 1:
                votes[ut[0]][uv[0]] += 1
        # best candidate per side, accepted only when mutual and similar
        best_for_v: dict[int, tuple[int, int]] = {}
        for t_addr, cands in votes.items():
            for v_addr, n in cands.items():
                if n > best_for_v.get(v_addr, (0, 0))[0]:
                    best_for_v[v_addr] = (n, t_addr)
        added = 0
        for t_addr, cands in votes.items():
            (v_addr, n), *rest = cands.most_common(2)
            if rest and rest[0][1] == n:
                continue                     # tie
            if best_for_v.get(v_addr, (0, None))[1] != t_addr:
                continue                     # not mutual
            if t_addr in matched or v_addr in inverse or not similar(tip[t_addr], vp[v_addr]):
                continue
            matched[t_addr] = v_addr
            inverse[v_addr] = t_addr
            evidence[t_addr] = (rnd, n, cosine(tip[t_addr], vp[v_addr]))
            added += 1
        if not added:
            break
    return evidence


def best_by_code(t: Func, vp_sorted: list[Func], taken: set[int]) -> tuple[Func | None, float, float]:
    """Most similar unmatched VP1 function by mnemonic histogram (fallback)."""
    L = len(t.insns)
    scored = []
    for v in vp_sorted:
        if len(v.insns) < 0.6 * L:
            continue
        if len(v.insns) > 1.6 * L + 4:
            break
        if v.addr not in taken:
            scored.append((cosine(t, v), v))
    scored.sort(key=lambda s: -s[0])
    if not scored:
        return None, 0.0, 0.0
    return scored[0][1], scored[0][0], scored[0][0] - (scored[1][0] if len(scored) > 1 else 0.0)


def short_name(tip_name: str) -> str:
    """rex_spawn_supportPinataCreateGeneralEx_82575C30 -> supportPinataCreateGeneralEx."""
    n = ADDR_RE.sub("", tip_name)
    n = n[4:] if n.startswith("rex_") else n
    for prefix in ("spawn_", "player_", "math_"):
        if n.startswith(prefix) and len(n) > len(prefix):
            n = n[len(prefix):]
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tip", type=Path, default=DEFAULT_TIP, help=f"TiP-Recomp checkout (default {DEFAULT_TIP})")
    args = ap.parse_args()

    tip, vp = load(args.tip), load(ROOT)
    matched = anchors(tip, vp)
    n_anchor = len(matched)
    evidence = propagate(tip, vp, matched)
    print(f"functions: TiP {len(tip)}, Viva Pinata {len(vp)}; anchors {n_anchor}, "
          f"+{len(evidence)} through the call graph = {len(matched)} matched")

    taken = set(matched.values())
    vp_sorted = sorted(vp.values(), key=lambda f: len(f.insns))
    rows = []
    for t in sorted((f for f in tip.values() if f.name.startswith("rex_")), key=lambda f: f.name):
        if t.addr in matched:
            v = vp[matched[t.addr]]
            if t.addr in evidence:
                rnd, n, cos = evidence[t.addr]
                grade = "high" if n >= 2 or cos >= 0.9 else "medium"
                how = f"call graph (round {rnd}, {n} vote{'s' if n > 1 else ''})"
            else:
                cos, grade, how = 1.0, "exact", "identical code"
            rows.append((t, v, grade, cos, how))
        else:
            v, cos, margin = best_by_code(t, vp_sorted, taken)
            grade = "medium" if v and cos >= 0.9 and margin >= 0.03 else ("low" if v else "none")
            rows.append((t, v, grade, cos, f"code only (margin {margin:.2f})" if v else "no candidate"))

    order = {"exact": 0, "high": 1, "medium": 2, "low": 3, "none": 4}
    rows.sort(key=lambda r: (order[r[2]], r[0].name))
    counts = Counter(r[2] for r in rows)
    print("named TiP functions:", ", ".join(f"{k} {counts[k]}" for k in order if counts[k]))

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# Function names carried over from Trouble in Paradise\n\n"
                "Generated by `tools/port_names.py`; do not edit. TiP names come from "
                "[TiP-Recomp](https://github.com/SolarCookies/TiP-Recomp) (`config/retip_hooks.toml`, "
                "used with the author's permission). Matches compare the instruction sequences and call graphs of "
                "the two recompilations.\n\n"
                f"Functions: TiP {len(tip)}, Viva Pinata {len(vp)}. Matched: {n_anchor} identical anchors + "
                f"{len(evidence)} through the call graph = {len(matched)}.\n\n"
                "Grades: **exact** = identical code, unique in both games; **high** = reached through the call "
                "graph with 2+ votes or near-identical code; **medium/low** = verify by hand (a logging hook in "
                "game) before use.\n\n"
                "| TiP function | TiP addr | Viva Pinata | Grade | Similarity | How |\n"
                "| :-- | :-- | :-- | :-- | --: | :-- |\n")
        for t, v, grade, cos, how in rows:
            f.write(f"| `{short_name(t.name)}` | `{t.addr:08X}` | {f'`{v.name}`' if v else '—'} | {grade} | "
                    f"{cos:.2f} | {how} |\n")

    with HEADER.open("w", encoding="utf-8", newline="\n") as f:
        f.write("// vp_names.h - Viva Pinata engine functions identified from Trouble in Paradise.\n"
                "// Generated by tools/port_names.py (exact and high matches only); do not edit.\n"
                "// Full table: docs/NAMES_FROM_TIP.md. Names: TiP-Recomp (SolarCookies, with permission).\n"
                "//\n"
                "//   VP_FN_x   the generated function (weak alias: override it with REX_HOOK_RAW)\n"
                "//   VP_IMP_x  the original recompiled body (always callable)\n"
                "#pragma once\n\n")
        seen = set()
        for t, v, grade, cos, how in rows:
            n = short_name(t.name)
            if grade not in ("exact", "high") or not v or n in seen:
                continue
            seen.add(n)
            f.write(f"#define VP_FN_{n} {v.name}   // {grade}, TiP 0x{t.addr:08X}\n"
                    f"#define VP_IMP_{n} __imp__{v.name}\n")
    print(f"wrote {REPORT.relative_to(ROOT)} and {HEADER.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
