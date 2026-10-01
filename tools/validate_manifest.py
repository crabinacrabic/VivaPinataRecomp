#!/usr/bin/env python3
"""validate_manifest.py - pre-flight check of the ReXGlue v0.10 manifest chain.

Run before every F7 that touches vivapinata_manifest.toml or config/*.toml.
It does NOT run codegen. It re-implements the loader rules from
rexglue/win-amd64/include/rex/codegen/config.h and the KB (CROSS_INSPECTION_AO2
3.x) so schema mistakes surface here instead of halfway through a 10-minute
codegen in Visual Studio:

  * every include exists and parses (toml 1.0)
  * keys are limited to what RecompilerConfig::LoadFromTable understands
  * [functions]/[rexcrt]/[[midasm_hook]] addresses are 4-byte aligned and
    inside the image (0x82000000..0x82B90000, from the XEX header)
  * size/end are mutually exclusive; parent chunks point at known functions
  * [rexcrt] names come from the SDK's accepted list; the heap group is
    all-or-nothing
  * setjmp/longjmp are set together
  * merged view is printed so what codegen will actually see is visible

Exit code 0 = clean, 1 = errors, 2 = warnings only.

Usage:  python tools/validate_manifest.py [path/to/manifest.toml]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:  # pragma: no cover
    import toml as _toml  # type: ignore

    class tomllib:  # type: ignore
        @staticmethod
        def load(fp):
            return _toml.loads(fp.read().decode("utf-8"))

        TOMLDecodeError = _toml.TomlDecodeError  # type: ignore


IMAGE_BASE = 0x82000000
IMAGE_SIZE = 0x00B90000
IMAGE_END = IMAGE_BASE + IMAGE_SIZE

PROJECT_KEYS = {"name", "sdk_version", "game_root"}
ENTRY_SCALAR_KEYS = {
    "file_path", "out_directory_path", "template_dir", "includes", "is_dll",
    "skip_lr", "skip_msr", "ctr_as_local", "xer_as_local", "cr_as_local",
    "reserved_as_local", "non_argument_as_local", "non_volatile_as_local",
    "generate_exception_handlers", "setjmp_address", "longjmp_address",
    "indirect_calls",
}
ENTRY_TABLE_KEYS = {"analysis", "functions", "rexcrt", "globals"}
ENTRY_ARRAY_KEYS = {"switch_tables", "invalid_instructions", "midasm_hook"}
ANALYSIS_KEYS = {"max_jump_extension", "data_region_threshold",
                 "large_function_threshold", "exception_handler_funcs"}
FUNCTION_KEYS = {"name", "size", "end", "parent", "share_registers"}
MIDASM_KEYS = {"address", "name", "registers", "after_instruction", "return",
               "return_on_true", "return_on_false", "jump_address",
               "jump_address_on_true", "jump_address_on_false"}
MIDASM_OUTCOMES = {"return", "return_on_true", "return_on_false",
                   "jump_address", "jump_address_on_true", "jump_address_on_false"}
SWITCH_KEYS = {"address", "register", "labels"}
INVALID_KEYS = {"data", "size"}

REXCRT_NAMES = {
    # memory
    "memcpy", "memmove", "memset", "memchr", "XMemCpy", "XMemSet", "XMemSet128",
    "memset_vmx", "memcpy_s", "memmove_s",
    # string
    "strncmp", "strncpy", "strchr", "strstr", "strrchr", "strtok", "_stricmp",
    "strcpy_s", "lstrlenA", "lstrcpyA", "lstrcpynA", "lstrcatA", "lstrcmpiA",
    "wcslen", "wcscmp", "wcsncmp", "wcscoll", "wcschr", "wcsrchr", "wcscpy",
    "wcsncpy", "wcsncpy_s", "wcsstr",
    # file
    "CreateFileA", "ReadFile", "WriteFile", "SetFilePointer", "GetFileSize",
    "GetFileSizeEx", "SetEndOfFile", "FlushFileBuffers", "DeleteFileA",
    "CloseHandle", "FindFirstFileA", "FindNextFileA", "FindClose",
    "CreateDirectoryA", "MoveFileA", "SetFileAttributesA", "GetFileAttributesA",
    "GetFileAttributesExA", "SetFilePointerEx", "SetFileTime", "CompareFileTime",
    "CopyFileA", "RemoveDirectoryA", "GetFileType",
    # fibers
    "ConvertThreadToFiber", "ConvertFiberToThread", "CreateFiber", "DeleteFiber",
    "SwitchToFiber",
    # heap (all-or-nothing)
    "RtlAllocateHeap", "RtlFreeHeap", "RtlSizeHeap", "RtlReAllocateHeap",
}
HEAP_GROUP = {"RtlAllocateHeap", "RtlFreeHeap", "RtlSizeHeap", "RtlReAllocateHeap"}
PPC_REGS = (
    {f"r{i}" for i in range(32)} | {f"f{i}" for i in range(32)} |
    {f"cr{i}" for i in range(8)} | {f"v{i}" for i in range(128)} |
    {"ctr", "xer", "fpscr", "lr"}
)


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def err(self, where: str, msg: str) -> None:
        self.errors.append(f"{where}: {msg}")

    def warn(self, where: str, msg: str) -> None:
        self.warnings.append(f"{where}: {msg}")


def parse_addr(v, where: str, rep: Report) -> int | None:
    """TOML keys are strings ("0x82ABCDEF"); values may be ints."""
    if isinstance(v, int):
        a = v
    else:
        try:
            a = int(str(v), 0)
        except ValueError:
            rep.err(where, f"not an address: {v!r}")
            return None
    if a % 4:
        rep.err(where, f"0x{a:08X} is not 4-byte aligned")
    if not (IMAGE_BASE <= a < IMAGE_END):
        rep.err(where, f"0x{a:08X} outside image 0x{IMAGE_BASE:08X}..0x{IMAGE_END:08X}")
    return a


def load_toml(path: Path, rep: Report) -> dict | None:
    try:
        with path.open("rb") as fp:
            return tomllib.load(fp)
    except FileNotFoundError:
        rep.err(str(path), "file not found")
    except tomllib.TOMLDecodeError as e:  # type: ignore[attr-defined]
        rep.err(str(path), f"TOML parse error: {e}")
    return None


class Merged:
    """Mirror of RecompilerConfig after LoadFromTable + includes."""

    def __init__(self) -> None:
        self.scalars: dict[str, tuple[object, str]] = {}   # key -> (value, source)
        self.analysis: dict[str, tuple[object, str]] = {}
        self.functions: dict[int, tuple[dict, str]] = {}
        self.rexcrt: dict[str, tuple[int, str]] = {}
        self.globals: dict[int, tuple[dict, str]] = {}
        self.midasm: dict[int, tuple[dict, str]] = {}
        self.switch: dict[int, tuple[dict, str]] = {}
        self.invalid: dict[int, tuple[dict, str]] = {}
        self.loaded: list[str] = []


def absorb(tbl: dict, src: Path, base_dir: Path, m: Merged, rep: Report,
           depth: int = 0, seen: set[Path] | None = None) -> None:
    seen = seen or set()
    rs = src.resolve()
    if rs in seen:
        rep.err(str(src), "circular include")
        return
    if depth > 8:
        rep.err(str(src), "include depth exceeded")
        return
    seen = seen | {rs}
    m.loaded.append(str(src))
    where = src.name

    for key, val in tbl.items():
        if key == "includes":
            if not isinstance(val, list):
                rep.err(where, "includes must be an array")
                continue
            for inc in val:
                p = (base_dir / inc)
                t = load_toml(p, rep)
                if t is not None:
                    absorb(t, p, p.parent, m, rep, depth + 1, seen)
            continue

        if key in ENTRY_SCALAR_KEYS:
            if key in m.scalars:
                rep.warn(where, f"{key} overrides value from {m.scalars[key][1]} (last-wins)")
            m.scalars[key] = (val, where)
            continue

        if key == "analysis":
            for ak, av in val.items():
                if ak not in ANALYSIS_KEYS:
                    rep.err(where, f"[analysis] unknown key '{ak}'")
                m.analysis[ak] = (av, where)
            continue

        if key == "functions":
            for k, fv in val.items():
                a = parse_addr(k, f"{where} [functions]", rep)
                if a is None:
                    continue
                if not isinstance(fv, dict):
                    rep.err(where, f"[functions] 0x{a:08X} must be an inline table")
                    continue
                for fk in fv:
                    if fk not in FUNCTION_KEYS:
                        rep.err(where, f"[functions] 0x{a:08X} unknown key '{fk}'")
                if fv.get("size") and fv.get("end"):
                    rep.err(where, f"[functions] 0x{a:08X}: size and end are mutually exclusive")
                if "end" in fv:
                    e = parse_addr(fv["end"], f"{where} [functions] 0x{a:08X}.end", rep)
                    if e is not None and e <= a:
                        rep.err(where, f"[functions] 0x{a:08X}: end 0x{e:08X} <= address")
                if "parent" in fv:
                    parse_addr(fv["parent"], f"{where} [functions] 0x{a:08X}.parent", rep)
                if "name" in fv and not str(fv["name"]).isidentifier():
                    rep.err(where, f"[functions] 0x{a:08X}: name {fv['name']!r} is not a C identifier")
                if a in m.functions:
                    rep.warn(where, f"[functions] 0x{a:08X} already defined in {m.functions[a][1]} (last-wins)")
                m.functions[a] = (fv, where)
            continue

        if key == "rexcrt":
            for name, av in val.items():
                if name not in REXCRT_NAMES:
                    rep.err(where, f"[rexcrt] '{name}' is not a name the SDK implements")
                a = parse_addr(av, f"{where} [rexcrt] {name}", rep)
                if a is not None:
                    m.rexcrt[name] = (a, where)
            continue

        if key == "globals":
            for k, gv in val.items():
                a = parse_addr(k, f"{where} [globals]", rep)
                if a is not None:
                    m.globals[a] = (gv, where)
            continue

        if key == "midasm_hook":
            for h in val:
                if "address" not in h or "name" not in h:
                    rep.err(where, "[[midasm_hook]] needs address and name")
                    continue
                a = parse_addr(h["address"], f"{where} [[midasm_hook]] {h.get('name')}", rep)
                if a is None:
                    continue
                for hk in h:
                    if hk not in MIDASM_KEYS:
                        rep.err(where, f"[[midasm_hook]] {h['name']}: unknown key '{hk}'")
                if not str(h["name"]).isidentifier():
                    rep.err(where, f"[[midasm_hook]] name {h['name']!r} is not a C identifier")
                for r in h.get("registers", []):
                    if r not in PPC_REGS:
                        rep.err(where, f"[[midasm_hook]] {h['name']}: unknown register '{r}'")
                outcomes = [k for k in MIDASM_OUTCOMES if h.get(k)]
                if len(outcomes) > 1:
                    rep.err(where, f"[[midasm_hook]] {h['name']}: more than one outcome {outcomes}")
                for jk in ("jump_address", "jump_address_on_true", "jump_address_on_false"):
                    if jk in h:
                        parse_addr(h[jk], f"{where} [[midasm_hook]] {h['name']}.{jk}", rep)
                if a in m.midasm:
                    rep.warn(where, f"[[midasm_hook]] 0x{a:08X} deduplicated against {m.midasm[a][1]}")
                m.midasm[a] = (h, where)
            continue

        if key == "switch_tables":
            for s in val:
                for sk in s:
                    if sk not in SWITCH_KEYS:
                        rep.err(where, f"[[switch_tables]] unknown key '{sk}'")
                a = parse_addr(s.get("address", 0), f"{where} [[switch_tables]]", rep)
                if a is None:
                    continue
                if s.get("register") not in PPC_REGS:
                    rep.err(where, f"[[switch_tables]] 0x{a:08X}: bad register {s.get('register')!r}")
                for lbl in s.get("labels", []):
                    parse_addr(lbl, f"{where} [[switch_tables]] 0x{a:08X}.labels", rep)
                m.switch[a] = (s, where)
            continue

        if key == "invalid_instructions":
            for iv in val:
                for ik in iv:
                    if ik not in INVALID_KEYS:
                        rep.err(where, f"[[invalid_instructions]] unknown key '{ik}'")
                a = parse_addr(iv.get("data", 0), f"{where} [[invalid_instructions]]", rep)
                if a is not None:
                    m.invalid[a] = (iv, where)
            continue

        rep.err(where, f"unknown key '{key}' at entrypoint level")


def cross_checks(m: Merged, manifest_dir: Path, rep: Report) -> None:
    fp = m.scalars.get("file_path")
    if not fp:
        rep.err("manifest", "[entrypoint].file_path missing")
    elif not (manifest_dir / str(fp[0])).exists():
        rep.err("manifest", f"file_path '{fp[0]}' does not exist")
    if "out_directory_path" not in m.scalars:
        rep.err("manifest", "[entrypoint].out_directory_path missing")

    sj = m.scalars.get("setjmp_address", (0, ""))[0]
    lj = m.scalars.get("longjmp_address", (0, ""))[0]
    if bool(sj) != bool(lj):
        rep.err("manifest", "setjmp_address and longjmp_address must be set together")
    for k in ("setjmp_address", "longjmp_address"):
        if m.scalars.get(k, (0, ""))[0]:
            parse_addr(m.scalars[k][0], f"manifest {k}", rep)

    heap = HEAP_GROUP & set(m.rexcrt)
    if heap and heap != HEAP_GROUP:
        rep.err("rexcrt", f"heap group is all-or-nothing; present {sorted(heap)}, "
                          f"missing {sorted(HEAP_GROUP - heap)}")

    # a rexcrt address must not also be renamed/sized as a plain function
    for name, (a, src) in m.rexcrt.items():
        if a in m.functions:
            rep.err("rexcrt", f"{name} = 0x{a:08X} is also in [functions] ({m.functions[a][1]})")

    # chunk parents must exist as functions
    for a, (fv, src) in m.functions.items():
        if "parent" in fv:
            p = int(str(fv["parent"]), 0) if not isinstance(fv["parent"], int) else fv["parent"]
            if p not in m.functions:
                rep.warn(src, f"[functions] 0x{a:08X}.parent 0x{p:08X} is not listed; "
                              "codegen must discover it")

    # A midasm name is the host function called at that address, so one name may
    # serve several addresses (the vpkd3d128 fix uses one at 30), as long as every
    # use passes the same register kinds (the C++ signature) and the same outcome.
    def midasm_shape(h: dict) -> tuple:
        kinds = tuple(re.sub(r"\d+$", "", str(r)) for r in h.get("registers", []))
        outcome = tuple(sorted(k for k in h if k in ("return", "return_on_true", "return_on_false",
                                                     "jump_address", "jump_address_on_true",
                                                     "jump_address_on_false")))
        return kinds, outcome

    names: dict[str, tuple[int, tuple]] = {}
    for a, (h, src) in m.midasm.items():
        n = h["name"]
        shape = midasm_shape(h)
        if n in names and names[n][1] != shape:
            rep.err(src, f"[[midasm_hook]] name '{n}' at 0x{names[n][0]:08X} and 0x{a:08X} with different "
                         f"registers/outcome {names[n][1]} vs {shape} (one C++ function cannot take both)")
        names.setdefault(n, (a, shape))

    # function names must be unique too
    fnames: dict[str, int] = {}
    for a, (fv, src) in m.functions.items():
        n = fv.get("name")
        if n:
            if n in fnames:
                rep.err(src, f"[functions] name '{n}' used at 0x{fnames[n]:08X} and 0x{a:08X}")
            fnames[n] = a


def main(argv: list[str]) -> int:
    manifest = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent / "vivapinata_manifest.toml"
    rep = Report()
    root = load_toml(manifest, rep)
    if root is None:
        for e in rep.errors:
            print("ERROR", e)
        return 1

    proj = root.get("project")
    if not isinstance(proj, dict):
        rep.err(manifest.name, "[project] section missing")
    else:
        for k in proj:
            if k not in PROJECT_KEYS:
                rep.err(manifest.name, f"[project] unknown key '{k}'")
        if not proj.get("name", "").isidentifier():
            rep.err(manifest.name, f"[project].name {proj.get('name')!r} must be a C identifier")

    entry = root.get("entrypoint")
    m = Merged()
    if not isinstance(entry, dict):
        rep.err(manifest.name, "[entrypoint] section missing")
    else:
        absorb(entry, manifest, manifest.parent, m, rep)
        cross_checks(m, manifest.parent, rep)

    for k in root:
        if k not in {"project", "entrypoint", "modules"}:
            rep.err(manifest.name, f"unknown top-level key '{k}'")

    print(f"manifest : {manifest}")
    print(f"project  : {proj.get('name') if isinstance(proj, dict) else '?'}  "
          f"sdk_version={proj.get('sdk_version') if isinstance(proj, dict) else '?'}")
    print("loaded   :")
    for f in m.loaded:
        print(f"           {Path(f).name}")
    print("codegen flags (merged):")
    for k in sorted(ENTRY_SCALAR_KEYS - {"includes", "file_path", "out_directory_path", "template_dir"}):
        if k in m.scalars:
            v, src = m.scalars[k]
            print(f"           {k:<28} = {v!r:<12} ({src})")
    if m.analysis:
        print("analysis :")
        for k, (v, src) in m.analysis.items():
            print(f"           {k:<28} = {v!r:<12} ({src})")
    print(f"functions: {len(m.functions)}  named: {sum(1 for f, _ in m.functions.values() if f.get('name'))}"
          f"  chunks: {sum(1 for f, _ in m.functions.values() if f.get('parent'))}")
    print(f"rexcrt   : {len(m.rexcrt)}  {sorted(m.rexcrt) if m.rexcrt else ''}")
    print(f"midasm   : {len(m.midasm)}   switch_tables: {len(m.switch)}   invalid_instructions: {len(m.invalid)}"
          f"   globals: {len(m.globals)}")

    for w in rep.warnings:
        print("WARN ", w)
    for e in rep.errors:
        print("ERROR", e)
    if rep.errors:
        print(f"\nFAILED: {len(rep.errors)} error(s), {len(rep.warnings)} warning(s)")
        return 1
    if rep.warnings:
        print(f"\nOK with {len(rep.warnings)} warning(s)")
        return 2
    print("\nOK - manifest chain is clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
