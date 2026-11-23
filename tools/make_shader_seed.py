#!/usr/bin/env python3
"""make_shader_seed.py - build assets/shader_seed from shader caches that were played on.

Why
---
The SDK compiles a shader the first time a draw needs it: the stutter of a
first run. It keeps what it compiled in <cache>\\shaders\\shareable and replays
it at the next start, so later runs are smooth:

    4D5307F2.xsh                 the microcode of every shader the game used
    4D5307F2.<path>.<api>.xpso   the pipelines built from them

The microcode is game code and cannot be shipped. But it lies in the game
files: in zlib streams of Beta\\bundles_packages\\*.pkg and in the loaded
default.xex. This tool writes down where (hashes, sizes, offsets) and copies
the pipeline descriptions, which are not game code. On a player's first run,
src/shader_seed/shader_seed.h cuts every shader out of their own game files,
checks its hash and fills the storage before the SDK opens it.

Some vertex shaders reach the GPU with a few words changed: the D3D runtime
patches their vertex fetches when it binds them to a vertex layout. For those
the seed holds the place of the original and an XOR mask per changed word,
like a ROM patch. Patches that would change more than a third of a shader (or
more than 48 words) are not written: such a shader is left out.

Output (assets/shader_seed):
    4D5307F2.seed.txt          where every shader lies; format in its header
    4D5307F2.<path>.<api>.xpso the pipelines whose shaders were all found

Usage
-----
    python tools/make_shader_seed.py --cache <dir> [--cache <dir> ...]
        [--image <file>] [--game game_files] [--out assets/shader_seed]
        [--pipelines rtv.d3d12 [fsi.vk ...]]

--cache      a folder with 4D5307F2.xsh: <cache root>\\shaders\\shareable (or
             the cache root). Several folders are merged.
--image      the loaded default.xex image, saved by a run with
             --vp_dev_dump_image=<file>. About 1 shader in 9 lives only there:
             default.xex is compressed on disk. The file is game code: keep it
             out of the repository.
--pipelines  the pipeline lists to ship (default: rtv.d3d12, the Fast mode).
             All located shaders are written either way. fsi.vk (Accurate) is
             left out for now: in one lab run its 1,501 seeded pipelines gave
             more slow ticks, not fewer, and a PC whose driver cache has not
             seen them spends up to a minute on them at startup.

Pipelines are recorded per GPU feature set; the ones in this repository come
from an AMD RX 6600. The SDK skips stored pipelines a GPU does not support.
"""
import argparse
import os
import re
import struct
import sys
import time
import zlib

import numpy as np

TITLE = "4D5307F2"
XSH_MAGIC = b"XESH"
XPSO_MAGIC = b"XEPS"
# Bytes per stored pipeline: an 8-byte hash, then the API's PipelineDescription.
RECORD_BYTES = {"vk": 68, "d3d12": 72}
IMAGE_BASE = 0x82000000
PLAIN_NAME = re.compile(r"^[A-Za-z0-9._-]+$")  # what shader_seed.h accepts
ZLIB_HEADER = re.compile(rb"\x78[\x01\x5e\x9c\xda]")
# Cost of using one more stream, in inflated bytes: opening and seeking a package.
STREAM_OVERHEAD = 64 * 1024
MAX_PATCH_WORDS = 48


def fail(message):
    sys.exit(f"make_shader_seed: {message}")


def shareable_dir(path):
    for candidate in (path, os.path.join(path, "shaders", "shareable")):
        if os.path.isfile(os.path.join(candidate, f"{TITLE}.xsh")):
            return candidate
    fail(f"no {TITLE}.xsh in {path}")


def load_xsh(path):
    """Shader storage: 'XESH', the version byte-swapped, then records of
    {u64 XXH3, u32 dwords | pixel << 31, microcode in guest byte order}."""
    data = open(path, "rb").read()
    if len(data) < 8 or data[:4] != XSH_MAGIC:
        fail(f"{path} is not a shader storage file")
    version = int.from_bytes(data[4:8], "big")
    shaders, at = [], 8
    while at + 12 <= len(data):
        ucode_hash, size_and_type = struct.unpack_from("<QI", data, at)
        dwords = size_and_type & 0x7FFFFFFF
        code = data[at + 12:at + 12 + dwords * 4]
        if not dwords or len(code) < dwords * 4:
            break  # torn record at the end
        shaders.append((ucode_hash, size_and_type >> 31, dwords, code))
        at += 12 + dwords * 4
    return version, shaders


def load_xpso(path, record):
    data = open(path, "rb").read()
    if len(data) < 12 or data[:4] != XPSO_MAGIC:
        return None, []
    count = (len(data) - 12) // record
    return data[:12], [data[12 + i * record:12 + (i + 1) * record] for i in range(count)]


def zlib_streams(data):
    """Every zlib stream in a package: (offset, packed bytes, inflated data)."""
    view = memoryview(data)
    resume = 0
    for match in ZLIB_HEADER.finditer(data):
        start = match.start()
        if start < resume or ((data[start] << 8) | data[start + 1]) % 31:
            continue
        inflater = zlib.decompressobj()
        parts, at = [], start
        try:
            while not inflater.eof and at < len(data):
                chunk = view[at:at + (1 << 20)]
                parts.append(inflater.decompress(chunk))
                at += len(chunk)
        except zlib.error:
            continue
        if not inflater.eof:
            continue
        plain = b"".join(parts)
        if len(plain) < 64:
            continue
        end = at - len(inflater.unused_data)
        resume = end
        yield start, end - start, plain


def unique_streams(package_dir):
    """(package, offset, packed bytes, inflated data) of every stream, skipping
    streams whose data an earlier one already had."""
    seen = set()
    for name in sorted(n for n in os.listdir(package_dir) if n.lower().endswith(".pkg")):
        data = open(os.path.join(package_dir, name), "rb").read()
        for offset, packed, plain in zlib_streams(data):
            key = (len(plain), zlib.crc32(plain))
            if key not in seen:
                seen.add(key)
                yield name, offset, packed, plain


def word_keys(data):
    """The 8 bytes at every word of `data`, as big-endian integers."""
    words = len(data) // 4
    array = np.frombuffer(data, dtype=">u4", count=words).astype(np.uint64)
    return (array[:-1] << np.uint64(32)) | array[1:]


def find_exact(package_dir, wanted, streams):
    """Where each wanted shader lies, word aligned, in the package streams.
    Appends the streams with a hit to `streams` and returns, per shader index,
    a list of (stream, offset in the inflated stream)."""
    by_prefix = {}
    for index, code in wanted.items():
        by_prefix.setdefault(code[:8], []).append(index)
    prefixes = np.array(sorted(int.from_bytes(p, "big") for p in by_prefix), dtype=np.uint64)
    hits, count = {}, 0
    for name, offset, packed, plain in unique_streams(package_dir):
        count += 1
        if len(plain) < 12:
            continue
        stream = None
        for word in np.nonzero(np.isin(word_keys(plain), prefixes))[0].tolist():
            at = word * 4
            for index in by_prefix[plain[at:at + 8]]:
                code = wanted[index]
                if plain[at:at + len(code)] == code:
                    if stream is None:
                        stream = len(streams)
                        streams.append((name, offset, packed))
                    hits.setdefault(index, []).append((stream, at))
    print(f"packages: {count} unique zlib streams searched")
    return hits


def near_matches(remaining, sources):
    """For each shader in `remaining` ({index: code}), the place with the fewest
    differing words among the places where at least an eighth of its 8-byte
    windows match. `sources` yields (source id, data).
    Returns {index: (source id, offset, [(word, xor mask)])}."""
    windows = {}
    for index, code in remaining.items():
        for k in range(0, len(code) - 7, 4):
            window = code[k:k + 8]
            if window[:4] != b"\0\0\0\0" and window[4:] != b"\0\0\0\0":
                windows.setdefault(window, []).append((index, k))
    keys = np.array(sorted(int.from_bytes(w, "big") for w in windows), dtype=np.uint64)
    best = {}
    for source, data in sources:
        if len(data) < 12:
            continue
        votes = {}
        for word in np.nonzero(np.isin(word_keys(data), keys))[0].tolist():
            at = word * 4
            for index, k in windows.get(data[at:at + 8], ()):
                votes[(index, at - k)] = votes.get((index, at - k), 0) + 1
        for (index, base), count in votes.items():
            code = remaining[index]
            words = len(code) // 4
            if count < max(2, (words - 1) // 8) or base < 0 or base + len(code) > len(data):
                continue
            original = struct.unpack_from(f">{words}I", data, base)
            target = struct.unpack(f">{words}I", code)
            patch = [(i, a ^ b) for i, (a, b) in enumerate(zip(original, target)) if a != b]
            if index not in best or len(patch) < len(best[index][2]):
                best[index] = (source, base, patch)
    return best


def patched(data, patch):
    if not patch:
        return data
    words = list(struct.unpack(f">{len(data) // 4}I", data))
    for index, mask in patch:
        words[index] ^= mask
    return struct.pack(f">{len(words)}I", *words)


def choose_streams(hits, sizes):
    """Greedy weighted set cover: repeatedly take the stream prefix that covers
    the most shaders not covered yet per byte that has to be inflated.
    Returns {shader index: (stream, offset)} and {stream: bytes to inflate}."""
    per_stream = {}
    for index, places in hits.items():
        for stream, at in places:
            per_stream.setdefault(stream, []).append((at + sizes[index], index, at))
    for places in per_stream.values():
        places.sort()
    uncovered = set(hits)
    chosen, want = {}, {}
    while uncovered:
        best = None
        for stream, places in per_stream.items():
            have = want.get(stream, 0)
            count = 0
            for end, index, _ in places:
                if index not in uncovered:
                    continue
                count += 1
                cost = max(0, end - have) + (0 if stream in want else STREAM_OVERHEAD)
                score = (count / cost) if cost else float("inf")
                if best is None or score > best[0]:
                    best = (score, stream, end)
        _, stream, end = best
        want[stream] = max(want.get(stream, 0), end)
        for end2, index, at in per_stream[stream]:
            if end2 <= want[stream] and index in uncovered:
                uncovered.discard(index)
                chosen[index] = (stream, at)
    return chosen, want


def packed_needed(path, offset, packed, want):
    """Compressed bytes the decoder needs for the first `want` bytes, and those bytes."""
    with open(path, "rb") as f:
        f.seek(offset)
        raw = f.read(packed)
    inflater = zlib.decompressobj()
    plain = inflater.decompress(raw, want)
    if len(plain) < want:
        fail(f"stream at {offset} of {path} is shorter than {want} bytes")
    used = len(raw) - len(inflater.unconsumed_tail)
    return min(packed, used + 16), plain


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--cache", action="append", required=True)
    parser.add_argument("--image")
    parser.add_argument("--image-base", type=lambda s: int(s, 0), default=IMAGE_BASE)
    parser.add_argument("--game", default=os.path.join(root, "game_files"))
    parser.add_argument("--out", default=os.path.join(root, "assets", "shader_seed"))
    parser.add_argument("--pipelines", nargs="*", default=["rtv.d3d12"],
                        help="pipeline files to ship, as <path>.<api> (default: rtv.d3d12)")
    args = parser.parse_args()
    started = time.time()

    # 1. All shaders and pipelines of the given caches.
    version, shaders, order = None, {}, []
    pipelines = {name: (None, {}) for name in args.pipelines}
    for cache in args.cache:
        folder = shareable_dir(cache)
        cache_version, records = load_xsh(os.path.join(folder, f"{TITLE}.xsh"))
        if version is None:
            version = cache_version
        elif cache_version != version:
            print(f"skipping {folder}: shader storage version {cache_version:08X}, not {version:08X}")
            continue
        added = 0
        for ucode_hash, pixel, dwords, code in records:
            if ucode_hash not in shaders:
                shaders[ucode_hash] = (pixel, dwords, code)
                order.append(ucode_hash)
                added += 1
        print(f"{folder}: {len(records)} shaders ({added} new)")
        for name in args.pipelines:
            api = name.split(".")[-1]
            path = os.path.join(folder, f"{TITLE}.{name}.xpso")
            if api not in RECORD_BYTES or not os.path.isfile(path):
                continue
            header, records = load_xpso(path, RECORD_BYTES[api])
            known_header, known = pipelines[name]
            if header is None or (known_header is not None and header != known_header):
                print(f"  skipping {path}: another pipeline storage version")
                continue
            before = len(known)
            for record in records:
                known.setdefault(record[:8], record)
            pipelines[name] = (header, known)
            print(f"  {name}: {len(records)} pipelines ({len(known) - before} new)")
    if version is None:
        fail("no shader storage read")

    # 2. Shaders in the default.xex image cost nothing at run time: look there first.
    located = {}  # hash -> ("x", guest address, patch) or ("s", stream, offset, patch)
    image = open(args.image, "rb").read() if args.image else b""
    for ucode_hash in order:
        at = image.find(shaders[ucode_hash][2])
        if at >= 0:
            located[ucode_hash] = ("x", args.image_base + at, [])
    print(f"image: {len(located)} shaders found as they are")

    # 3. The rest from the packages, exactly.
    package_dir = os.path.join(args.game, "Beta", "bundles_packages")
    rest = [h for h in order if h not in located]
    wanted = {i: shaders[h][2] for i, h in enumerate(rest)}
    streams = []  # (package, offset, packed bytes) of every stream with a hit
    hits = find_exact(package_dir, wanted, streams)
    patches = {}

    # 4. Still missing: the original with a few words patched, in the image or a stream.
    remaining = {i: code for i, code in wanted.items() if i not in hits}
    if remaining:
        def sources():
            if image:
                yield "image", image
            for name, offset, packed, plain in unique_streams(package_dir):
                yield (name, offset, packed), plain

        near = near_matches(remaining, sources())
        for index, (source, base, patch) in near.items():
            words = len(remaining[index]) // 4
            if len(patch) > min(MAX_PATCH_WORDS, words // 3):
                continue
            if source == "image":
                located[rest[index]] = ("x", args.image_base + base, patch)
            else:
                if source not in streams:
                    streams.append(source)
                hits[index] = [(streams.index(source), base)]
                patches[index] = patch
        print(f"patched originals: {sum(1 for p in located.values() if p[2])} in the image, "
              f"{len(patches)} in the packages, {len(remaining) - len(near)} without a near match")

    # 5. Fewest bytes to inflate, then the cut points, checked with zlib.
    sizes = {i: len(code) for i, code in wanted.items()}
    chosen, want = choose_streams(hits, sizes)
    used = sorted(want, key=lambda s: (streams[s][0], streams[s][1]))
    renumber = {stream: n for n, stream in enumerate(used)}
    stream_lines, read_total, inflate_total = [], 0, 0
    for stream in used:
        name, offset, packed = streams[stream]
        if not PLAIN_NAME.match(name):
            fail(f"package name {name!r} is not a plain file name")
        needed, plain = packed_needed(os.path.join(package_dir, name), offset, packed, want[stream])
        for index, (s, at) in chosen.items():
            if s == stream and patched(plain[at:at + sizes[index]], patches.get(index)) != wanted[index]:
                fail(f"shader {rest[index]:016X} does not match its stream after the cut")
        stream_lines.append(f"stream {renumber[stream]} {name} {offset} {needed} {want[stream]}")
        read_total += needed
        inflate_total += want[stream]
    for index, (stream, at) in chosen.items():
        located[rest[index]] = ("s", renumber[stream], at, patches.get(index, []))
    for ucode_hash, place in located.items():
        if place[0] == "x":
            at = place[1] - args.image_base
            code = shaders[ucode_hash][2]
            if patched(image[at:at + len(code)], place[2]) != code:
                fail(f"shader {ucode_hash:016X} does not match the image")
    print(f"packages: {len(chosen)} shaders from {len(used)} streams of "
          f"{len(set(streams[s][0] for s in used))} packages; read {read_total / 1048576:.1f} MB, "
          f"inflate {inflate_total / 1048576:.1f} MB")

    # 6. Pipelines whose shaders were all found.
    os.makedirs(args.out, exist_ok=True)
    pipeline_lines = []
    for name in args.pipelines:
        header, known = pipelines[name]
        if header is None:
            continue
        api = name.split(".")[-1]
        keep = []
        for record in known.values():
            vertex, pixel = struct.unpack_from("<Q", record, 8)[0], struct.unpack_from("<Q", record, 24)[0]
            if vertex in located and (pixel == 0 or pixel in located):
                keep.append(record)
        file_name = f"{TITLE}.{name}.xpso"
        with open(os.path.join(args.out, file_name), "wb") as f:
            f.write(header + b"".join(keep))
        pipeline_lines.append(f"pipelines {file_name} {RECORD_BYTES[api]}")
        print(f"{file_name}: {len(keep)} of {len(known)} pipelines kept "
              f"({len(known) - len(keep)} need a shader that was not found)")

    # 7. The seed itself.
    lines = [
        f"# {TITLE}.seed.txt - written by tools/make_shader_seed.py, read by src/shader_seed/shader_seed.h",
        "# Where the shaders of a played-on shader cache lie in the game files.",
        "# Hashes, sizes, offsets and XOR masks only: no game code.",
        "#   stream <index> <file in Beta\\bundles_packages> <zlib offset> <bytes to read> <bytes to inflate>",
        "#   shader <XXH3, hex> <v|p> <dwords> s <stream> <offset in the inflated stream> [patch]",
        "#   shader <XXH3, hex> <v|p> <dwords> x <guest address in default.xex, hex> [patch]",
        "#   patch: d <count>, then <count> times <word index> <XOR mask of the big-endian word, hex>",
        "#   pipelines <file next to this one> <bytes per record>",
        "format 1",
        f"storage_version {version:08X}",
    ]
    lines += stream_lines
    for ucode_hash in sorted(located, key=lambda h: (located[h][0] == "s", located[h][1:-1], h)):
        pixel, dwords, _ = shaders[ucode_hash]
        place = located[ucode_hash]
        where = f"x {place[1]:08X}" if place[0] == "x" else f"s {place[1]} {place[2]}"
        patch = place[-1]
        if patch:
            where += f" d {len(patch)} " + " ".join(f"{i} {mask:08X}" for i, mask in patch)
        lines.append(f"shader {ucode_hash:016X} {'p' if pixel else 'v'} {dwords} {where}")
    lines += pipeline_lines
    with open(os.path.join(args.out, f"{TITLE}.seed.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    missing = [h for h in order if h not in located]
    for kind, label in ((0, "vertex"), (1, "pixel")):
        total = [h for h in order if shaders[h][0] == kind]
        print(f"{label} shaders: {sum(1 for h in total if h in located)} of {len(total)} located")
    if missing:
        print(f"not found: {len(missing)}: " +
              " ".join(f"{h:016X}/{'p' if shaders[h][0] else 'v'}{shaders[h][1]}" for h in missing))
    print(f"done in {time.time() - started:.0f} s -> {args.out}")


if __name__ == "__main__":
    main()
