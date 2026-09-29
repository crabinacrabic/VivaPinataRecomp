#!/usr/bin/env python3
"""make_russian_bnl.py - build a Russian text bundle for the Xbox 360 version
from the ZoG Team (Zone of Games, 2007) Russian translation of the PC version.

The translation is not stored in this repository. You need:
  * the PC game with the ZoG translation installed: <PC>\\bundles\\english.bnl
  * the original PC english.bnl that the installer backed up:
    <PC>\\Install_Rus\\backup\\bundles\\english.bnl
  * the Xbox 360 game_files (this project)

How it works
------------
A .bnl is a Rare CAFF container (magic "CAFF07.08.06.0036"): a header and
three zlib streams. The third stream holds the text as LSBL blocks:

    "LSBL" tag (PC) / "LBSL" (Xbox, byte-swapped)
    +0x20 u32 count
    +0x24 count x {u16 string hash, u32 offset in UTF-16 chars} + {0xFFFF, total}
    then UTF-16 strings (LE on PC, BE on Xbox 360), each 0-terminated

String hashes are the same on both platforms. ZoG translated in place: every
Russian string is padded with spaces to the length of the English one, so the
PC Russian and PC English files have the same layout. This script does the
same on the Xbox file: for each Xbox string it finds the PC English string
(same hash + same text, then looser matches), takes the Russian text, pads it
to the Xbox slot length and byte-swaps it. Nothing else in the file moves, so
the headers stay valid.

The text stream is rebuilt to exactly its original compressed size. Stream 0
records that size (offset 29; the decompressed size is at offset 9) and the
game inflates the stream in place, so a shorter stream padded with zeros - what
ZoG did on PC - gets its tail overwritten before it is read (garbage text,
crashes). See compress_exact(): stored blocks up front absorb the spare bytes.
Cyrillic UTF-16 compresses about 5% worse than the English text, so zlib -9
alone is too big; 7-Zip's deflate encoder leaves room (7-Zip required).
Editing stream 0 is not an option: it is inflated in place as well.

The Xbox font cache (Beta\\fontcacheabc\\02dd2188, used by english and
englishus) already has every Cyrillic glyph the translation uses.

english.bnl and englishus.bnl share one schema, so the launcher installs the
result over both (keeping .orig backups).

Usage:
  python tools/make_russian_bnl.py
      with the translation pack at translation/vp_russian.json (the Russian
      strings only, downloaded separately; no PC version needed), or
  python tools/make_russian_bnl.py --pc-ru "<PC>/bundles/english.bnl" ^
      --pc-en "<PC>/Install_Rus/backup/bundles/english.bnl"
      from the PC version with the ZoG translation installed.
Writes game_files/Beta/bundles/russian.bnl and russian_report.txt.
--export-pack FILE writes the translation pack from an existing russian.bnl.
Exit 0 on success, 1 on error.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUNDLES = ROOT / "game_files" / "Beta" / "bundles"

MAGIC = b"CAFF07.08.06.003"
# game_files/Beta/bundles/english.bnl from the tested disc (README).
X360_ENGLISH_SHA1 = "093795895fbe38e9faeeca85f5c1532067fd0ae0"

PACK = ROOT / "translation" / "vp_russian.json"
PACK_FORMAT = "vp-russian-1"
PACK_CREDITS = [
    "Russian translation: ZoG Team (zoneofgames.ru), Viva Pinata (PC, 2007), used with the team's permission",
    "Ported to the Xbox 360 version, plus Xbox-only lines: Viva Pinata Recomp",
]


class Caff:
    """Rare CAFF container: header, stream0, stream1, stream2 (text)."""

    def __init__(self, path: Path):
        self.path = path
        self.raw = path.read_bytes()
        if not self.raw.startswith(MAGIC):
            raise ValueError(f"{path}: not a CAFF file")
        self.big = self.raw[0x48] == 1  # 1 = big-endian (Xbox 360), 0 = PC
        e32 = ">I" if self.big else "<I"
        header_end = struct.unpack_from(e32, self.raw, 0x14)[0]
        comp0 = struct.unpack_from(e32, self.raw, 0x60)[0]
        comp1 = struct.unpack_from(e32, self.raw, 0x74)[0]
        self.text_offset = header_end + comp0 + comp1
        if self.raw[self.text_offset:self.text_offset + 1] != b"\x78":
            raise ValueError(f"{path}: no zlib stream at 0x{self.text_offset:X}")
        d = zlib.decompressobj()
        self.text = d.decompress(self.raw[self.text_offset:])
        self.text_size = len(self.raw) - self.text_offset  # compressed + padding


def parse_blocks(data: bytes, big: bool) -> list[list[tuple[int, int, int, str]]]:
    """Returns blocks of (hash, byte offset, slot length in chars, text)."""
    tag = b"LBSL" if big else b"LSBL"
    e16, e32 = (">H", ">I") if big else ("<H", "<I")
    enc = "utf-16-be" if big else "utf-16-le"
    blocks = []
    pos = 0
    while (t := data.find(tag, pos)) >= 0:
        pos = t + 4
        count = struct.unpack_from(e32, data, t + 0x20)[0]
        ent = t + 0x24
        if count > 20000 or struct.unpack_from(e16, data, ent + 6 * count)[0] != 0xFFFF:
            continue
        # The terminator's value is stored little-endian on both platforms.
        total = struct.unpack_from("<I", data, ent + 6 * count + 2)[0]
        base = ent + 6 * (count + 1)
        entries = [(struct.unpack_from(e16, data, ent + 6 * k)[0],
                    struct.unpack_from(e32, data, ent + 6 * k + 2)[0]) for k in range(count)]
        strings = []
        for k, (h, o) in enumerate(entries):
            end = entries[k + 1][1] if k + 1 < count else total
            a, b = base + 2 * o, base + 2 * end
            strings.append((h, a, end - o, data[a:b].decode(enc, errors="replace")))
        blocks.append(strings)
    return blocks


def norm(s: str) -> str:
    s = s.rstrip("\x00").strip()
    for a, b in (("ñ", "n"), ("Ñ", "N"), ("’", "'"), ("‘", "'"), ("“", '"'), ("”", '"'),
                 ("…", "..."), (" ", " ")):
        s = s.replace(a, b)
    return " ".join(s.split()).lower()


def body(s: str) -> str:
    """Visible text: without the terminator and the ZoG padding."""
    return s.rstrip("\x00").rstrip(" ")


# Xbox-only strings that the PC version (and so the ZoG translation) lacks or
# words differently (Xbox Live instead of Games for Windows, [A] instead of
# mouse buttons), plus a few ZoG lines shortened to fit the Xbox slot.
# Translated for this project with ZoG's terms ("пината", "Журнал", "Вирлм").
# Takes priority over the automatic matching; each must fit the English slot.
EXTRA_RU = {
    "Back to Garden": "Назад в сад",
    "\tPAUSED": "\tПАУЗА",
    "\t!CORRUPT!": "\t!ОШИБКА!",
    "\tPress START": "\tНажми START",
    "\tPlay time:": "\tНаиграно:",
    "System Link": "Лок. сеть",
    "System Link Menu": "Локальная сеть",
    "System Link Search": "Поиск в лок. сети",
    "System Link Quick Match": "Быстрая игра по сети",
    "System Link Create Game Menu": "Создать игру в лок. сети",
    "Live Search": "Поиск Live",
    "Live Quick Match": "Быстрая игра",
    "Play a Garden on System Link": "Сад по локальной сети",
    "Play a Garden on Xbox Live": "Сад через Xbox Live",
    "Xbox Live Garden Menu": "Сад в Xbox Live",
    "Learn About Online Safeguards": "О безопасности в сети",
    "Create Game - Set up your own garden on Xbox Live that others can join":
        "Создать игру - откройте свой сад в Xbox Live для других игроков",
    "Create Game - Set up your own garden that others on your network can join":
        "Создать игру - откройте свой сад для игроков в вашей сети",
    "Game Experience May Change During Online Play.": "В сетевой игре процесс может отличаться.",
    "© & (p) 2006 Microsoft Corporation. All Rights Reserved.\nDeveloped By Rare.":
        "© & (p) 2006 Microsoft Corporation. Все права защищены.\nРазработка: Rare.",
    "Controller disconnected": "Контроллер отключен",
    "Saving Content. Please don't turn off your console": "Сохранение. Не выключайте консоль",
    "Your save game is unreadable or invalid. This can be deleted from the Xbox Dashboard. "
    "Please press [A] to continue.":
        "Сохранение повреждено или не читается. Его можно удалить в Xbox Dashboard. "
        "Нажмите [A], чтобы продолжить.",
    "Your progress could not be saved - there may be insufficient space on your storage device. "
    "Use the Xbox Dashboard to manage space on your storage devices. Please press [A] to continue.":
        "Не удалось сохранить прогресс - возможно, на устройстве хранения мало места. "
        "Освободить место можно в Xbox Dashboard. Нажмите [A], чтобы продолжить.",
    "Your Xbox 360 Memory Unit has been disconnected, so no saving can be done to that location. "
    "Please press [A] to continue.":
        "Карта памяти Xbox 360 отключена, сохранять на неё нельзя. Нажмите [A], чтобы продолжить.",
    "Hello {text}. Sign in successful, welcome to Viva Piñata™!<br/> <br/><font color=[RED]>Warning:</font> "
    "Viva Piñata™ uses an autosave feature, and removing an Xbox 360 Memory Unit or powering down your "
    "Xbox 360 Console while [SAVING] is displayed may result in loss of data. Additionally, removing an "
    "Xbox 360 Memory Unit at any time will return you to the Title Screen.<br/> <br/>Please "
    "<font color=[GREEN]><i>press</i></font> [A] to continue.":
        "Привет, {text}. Вход выполнен, добро пожаловать в Viva Piñata™!<br/> <br/><font color=[RED]>"
        "Внимание:</font> в Viva Piñata™ есть автосохранение. Если извлечь карту памяти Xbox 360 или "
        "выключить консоль, пока на экране [SAVING], данные могут пропасть. А извлечение карты памяти "
        "в любой момент вернёт вас на титульный экран.<br/> <br/><font color=[GREEN]><i>Нажмите</i>"
        "</font> [A], чтобы продолжить.",
    "Welcome to the romancing minigame! You need to take control of your piñata and try to reach your "
    "partner without hitting any Loathers. The [LOATHER]s are creatures created by Professor Pester and "
    "they hate the piñatas being happy. Push [LS] in any direction to make your piñata move and remember "
    "that different piñata may move in different ways. If you hit a [LOATHER] you will lose a chance and "
    "waste some of your time. If you lose all of your chances the game is over. There are coins to "
    "collect too but be careful, you'll only be allowed to keep them if you finish the game. Hurry now, "
    "before the time runs out!":
        "Добро пожаловать в любовную миниигру! Управляйте своей пинатой и доберитесь до партнёра, не "
        "задев ни одного [LOATHER]. [LOATHER] - существа, созданные Профессором Пестером, они терпеть не "
        "могут счастливых пинат. Наклоняйте [LS], чтобы двигать пинату, и помните, что разные пинаты "
        "двигаются по-разному. Задели [LOATHER] - потеряли попытку и время. Когда попытки закончатся, "
        "игра окончена. Собирайте и монетки, но оставить их себе можно, только если пройдёте игру до "
        "конца. Скорее, пока не вышло время!",
    "Here are the experience petals. Can you see them around the clock? When you do something to help "
    "the garden the petals will turn blue. If you'd like some more information about the experience "
    "petals press [Y] or look in the Journal. Now let's see how many experience points you have gained "
    "from your work so far.":
        "Вот опытные лепестки. Видите их вокруг часов? Когда вы делаете что-то полезное для сада, "
        "лепестки становятся синими. Чтобы узнать о них больше, нажмите [Y] или загляните в Журнал. "
        "А теперь посмотрим, сколько очков опыта вы уже заработали.",
    "Take this GRASS SEED PACKET as a reward for getting your first visiting Whirlm piñata. It never "
    "runs out of seeds! Press [A] to close this alert then press [X] for the menu. Choose your new grass "
    "seed packet from there.":
        "Возьмите этот ПАКЕТ СЕМЯН ТРАВЫ в награду за первую пинату Вирлм в гостях. Семена в нём не "
        "кончаются! Нажмите [A], чтобы закрыть сообщение, потом [X] для меню и выберите там новый пакет.",
    "Yoo-hoo! Piñata Island's Magnificent Postal Services are at your disposal for all your needs, you "
    "know passing stuff on, getting some goodies off other Xbox Live gardeners. We can also keep any "
    "guests in check if you're having some co-op fun.":
        "Ю-ху! Чудесная Почтовая Служба Острова Пинат к вашим услугам: передаём посылки, получаем "
        "гостинцы от других садовников Xbox Live. А ещё присмотрим за гостями, если вы играете вместе.",
    "Now that you have raised a baby [Animal_worm] I feel like I can really trust you. I have two seeds "
    "that are ideal for someone starting a new garden. You can have either one - please choose a seed "
    "from either hand by moving [LS].":
        "Раз вы вырастили малыша [Animal_worm], я вам доверяю. У меня есть два семени, которые отлично "
        "подходят для нового сада. Можете взять любое - выберите руку, наклонив [LS].",
    "The buttons in the top right of the screen tell you what you can do with the shovel. I have three "
    "bits of advice:<br/>1. <font color=[RED]>You can clear the hard soil more quickly if you hold down "
    "[A].</font><br/>2. The shovel is old, but it won't break.<br/>3. If you <i>hit</i> piñata they can "
    "become <i>ill</i>.<br/>If you'd like more advice press [Y] now.":
        "Кнопки в правом верхнем углу экрана подсказывают, что можно делать лопатой. У меня есть три "
        "совета:<br/>1. <font color=[RED]>Твёрдую землю можно расчистить быстрее, если удерживать "
        "[A].</font><br/>2. Лопата старая, но не сломается.<br/>3. Если <i>ударить</i> пинату, она может "
        "<i>заболеть</i>.<br/>Ещё советы - нажмите [Y].",
    "O.K. Grab your shovel from the menu and turn this mess into your dream garden! Put the cursor on me "
    "and press [A] for help.":
        "Хорошо. Бери лопату в меню и преврати этот беспорядок в сад мечты! Наведи на меня курсор и "
        "нажми [A] для помощи.",
    "Jack of all trades, master of none' describes the Quackberry perfectly. It can walk, fly and swim, "
    "but not particularly well. It just can't loose that waddle.":
        "«За всё берётся, ничего толком не умеет» - это про Квакберри. Он ходит, летает и плавает, но всё "
        "так себе. И никак не избавится от походки вразвалку.",
    "You might think this is a bit strange, but those black spots on this seed almost look like eyes. "
    "Brrrr, gives me the creeps.":
        "Может, это и странно, но чёрные точки на этом семени похожи на глаза. Брр, у меня от них мурашки "
        "по коже.",
}


def digits(s: str) -> list[str]:
    return re.findall(r"\d+", s)


def similarity(a: str, b: str) -> float:
    return difflib.SequenceMatcher(None, norm(a), norm(b)).ratio()


def build_text(en_blocks, ru_blocks, x_blocks, x_text: bytes):
    """Writes the Russian strings into a copy of the Xbox text stream."""
    # PC English -> Russian, keyed several ways.
    exact: dict[tuple[int, str], str] = {}
    by_norm: dict[tuple[int, str], set[str]] = defaultdict(set)
    by_text: dict[str, set[str]] = defaultdict(set)
    by_hash: dict[int, set[tuple[str, str]]] = defaultdict(set)
    where: dict[tuple[int, str], int] = {}
    for bi, (eb, rb) in enumerate(zip(en_blocks, ru_blocks)):
        for (h, _, _, en), (_, _, _, ru) in zip(eb, rb):
            exact[(h, en)] = ru
            where[(h, en)] = bi
            by_norm[(h, norm(en))].add(body(ru))
            by_text[norm(en)].add(body(ru))
            by_hash[h].add((body(en), body(ru)))

    out = bytearray(x_text)
    stats: Counter[str] = Counter()
    untranslated: list[str] = []
    for xb in x_blocks:
        votes = Counter(where[(h, en)] for h, _, _, en in xb if (h, en) in where)
        pc_block = votes.most_common(1)[0][0] if votes else None
        for h, a, n, en in xb:
            if not body(en):
                continue
            ru = None
            if body(en) in EXTRA_RU:
                ru, kind = EXTRA_RU[body(en)], "extra"
            elif (h, en) in exact:
                ru, kind = body(exact[(h, en)]), "exact"
            elif len(by_norm.get((h, norm(en)), ())) == 1:
                ru, kind = next(iter(by_norm[(h, norm(en))])), "normalized"
            else:
                if pc_block is not None:
                    for (ph, _, _, pen), (_, _, _, pru) in zip(en_blocks[pc_block], ru_blocks[pc_block]):
                        if ph == h and similarity(en, pen) >= 0.8:
                            ru, kind = body(pru), "similar"
                            break
                if ru is None and len(by_text.get(norm(en), ())) == 1:
                    ru, kind = next(iter(by_text[norm(en)])), "text"
                if ru is None:
                    # Same hash anywhere in the file, same numbers, near-identical
                    # text: "Has eaten 1 bluebell flowers." (Xbox) is
                    # "Has eaten 1 bluebell flower." on PC.
                    best = max(((similarity(en, pen), pru) for pen, pru in by_hash.get(h, ())
                                if digits(pen) == digits(en)), default=None)
                    if best and best[0] >= 0.9:
                        ru, kind = best[1], "hash"
            if ru is None:
                stats["untranslated"] += 1
                untranslated.append(body(en))
                continue
            if len(ru) + 1 > n:
                stats["too_long"] += 1
                untranslated.append(body(en))
                continue
            text = ru + " " * (n - 1 - len(ru)) + "\x00"
            out[a:a + 2 * n] = text.encode("utf-16-be")
            stats[kind] += 1

    return bytes(out), stats, untranslated


def find_7zip() -> str | None:
    for p in (shutil.which("7z"), r"C:\Program Files\7-Zip\7z.exe", r"C:\Program Files (x86)\7-Zip\7z.exe"):
        if p and Path(p).exists():
            return p
    return None


def raw_deflate_zlib(data: bytes) -> bytes:
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    return c.compress(data) + c.flush()


def raw_deflate_7zip(data: bytes, seven_zip: str) -> bytes | None:
    """Raw deflate stream from 7-Zip's encoder (gzip container stripped)."""
    with tempfile.TemporaryDirectory() as tmp:
        src, gz = Path(tmp) / "text.bin", Path(tmp) / "text.gz"
        src.write_bytes(data)
        r = subprocess.run([seven_zip, "a", "-tgzip", "-mx9", str(gz), str(src)], capture_output=True)
        if r.returncode != 0 or not gz.exists():
            return None
        b = gz.read_bytes()
    if b[:3] != b"\x1f\x8b\x08":
        return None
    flags, i = b[3], 10
    if flags & 4:
        i += 2 + struct.unpack_from("<H", b, i)[0]
    for bit in (8, 16):  # file name, comment
        if flags & bit:
            i = b.index(b"\x00", i) + 1
    if flags & 2:
        i += 2
    return b[i:-8]


def stored_blocks(data: bytes) -> bytes:
    """Non-final deflate stored blocks (byte-aligned, 5 bytes of header each)."""
    out = bytearray()
    for i in range(0, len(data), 65535):
        chunk = data[i:i + 65535]
        out += b"\x00" + struct.pack("<HH", len(chunk), len(chunk) ^ 0xFFFF) + chunk
    return bytes(out)


EMPTY_STORED_BLOCK = b"\x00\x00\x00\xff\xff"


def compress_exact(data: bytes, size: int) -> bytes | None:
    """A zlib stream of exactly `size` bytes.

    The game inflates the text stream in place: the compressed bytes sit at the
    end of the output buffer and the output catches up with them only at the
    very end. A shorter stream padded with zeros (as ZoG did on PC) ends early,
    so its last bytes are overwritten before they are read and the tail of the
    text comes out as garbage (missing title-screen text, crashes). The stream
    must therefore fill the region exactly: the first k bytes of text go into
    stored (uncompressed) blocks, the rest is deflated, and empty stored blocks
    absorb the last few bytes. Stored data up front keeps the in-place order
    safe. zlib -9 is tried first; Cyrillic UTF-16 needs 7-Zip's encoder."""
    encoders = [("zlib", raw_deflate_zlib)]
    seven_zip = find_7zip()
    if seven_zip:
        encoders.append(("7-Zip", lambda d: raw_deflate_7zip(d, seven_zip)))
    trailer = struct.pack(">I", zlib.adler32(data))
    for name, encode in encoders:
        cache: dict[int, int] = {}

        def total(k: int) -> int:
            if k not in cache:
                deflated = encode(data[k:])
                cache[k] = (1 << 62) if deflated is None else \
                    2 + len(stored_blocks(data[:k])) + len(deflated) + 4
            return cache[k]

        if total(0) > size:
            continue
        # total(k) grows ~0.8 byte per byte moved into the stored prefix.
        lo, hi = 0, min(len(data), int((size - total(0)) / 0.6) + 1)
        while total(hi) <= size and hi < len(data):
            lo, hi = hi, min(len(data), hi * 2)
        while hi - lo > 1:  # largest k with total(k) <= size
            mid = (lo + hi) // 2
            if total(mid) <= size:
                lo = mid
            else:
                hi = mid
        for k in range(lo, max(-1, lo - 200), -1):
            if total(k) > size or (size - total(k)) % 5:
                continue
            body = stored_blocks(data[:k]) + encode(data[k:])
            stream = b"\x78\x9c" + EMPTY_STORED_BLOCK * ((size - total(k)) // 5) + body + trailer
            if len(stream) == size and zlib.decompress(stream) == data:
                print(f"  {name}: {k} bytes stored, {len(stream)} bytes total")
                return stream
    print(f"error: cannot build a {size}-byte text stream"
          + ("" if seven_zip else " (install 7-Zip: zlib alone is not enough for Cyrillic)"),
          file=sys.stderr)
    return None


def layout(blocks) -> list[list[tuple[int, int, int]]]:
    return [[(h, a, n) for h, a, n, _ in b] for b in blocks]


# --- translation pack ---------------------------------------------------------
# The file players download: only the Russian strings, keyed by block and string
# index plus a CRC32 of the English string (no game text). With it, russian.bnl
# is built from the player's own english.bnl; the PC version is not needed.

def en_crc(en: str) -> str:
    return f"{zlib.crc32(en.encode('utf-8')):08x}"


def export_pack(x360: Caff, russian_path: Path, pack_path: Path) -> int:
    try:
        ru = Caff(russian_path)
    except (OSError, ValueError) as e:
        print(f"error: {e} (build russian.bnl first)", file=sys.stderr)
        return 1
    x_blocks, ru_blocks = parse_blocks(x360.text, True), parse_blocks(ru.text, True)
    if layout(x_blocks) != layout(ru_blocks):
        print("error: russian.bnl does not match this english.bnl", file=sys.stderr)
        return 1
    strings = []
    for bi, (xb, rb) in enumerate(zip(x_blocks, ru_blocks)):
        for si, ((_, _, _, en), (_, _, _, r)) in enumerate(zip(xb, rb)):
            if body(en) and body(r) != body(en):
                strings.append({"b": bi, "s": si, "crc": en_crc(body(en)), "ru": body(r)})
    pack = {
        "format": PACK_FORMAT,
        "game": "Viva Pinata (Xbox 360), Title ID 4D5307F2",
        "source": "Beta/bundles/english.bnl",
        "source_sha1": X360_ENGLISH_SHA1,
        "credits": PACK_CREDITS,
        "strings": strings,
    }
    pack_path.parent.mkdir(parents=True, exist_ok=True)
    pack_path.write_text(json.dumps(pack, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"{pack_path}: {len(strings)} Russian strings")
    return 0


def text_from_pack(pack_path: Path, x_blocks, x_text: bytes):
    """Writes the pack's strings into a copy of the Xbox text stream."""
    pack = json.loads(pack_path.read_text(encoding="utf-8"))
    if not isinstance(pack, dict) or pack.get("format") != PACK_FORMAT:
        raise ValueError(f"{pack_path}: not a {PACK_FORMAT} translation pack")
    out = bytearray(x_text)
    stats: Counter[str] = Counter()
    mismatched: list[str] = []
    for s in pack.get("strings", []):
        b, i, ru = s.get("b"), s.get("s"), s.get("ru", "")
        if not (isinstance(b, int) and 0 <= b < len(x_blocks) and isinstance(i, int) and 0 <= i < len(x_blocks[b])):
            stats["mismatch"] += 1
            mismatched.append(f"block {b} string {i}: not in this english.bnl")
            continue
        _, a, n, en = x_blocks[b][i]
        if en_crc(body(en)) != s.get("crc"):
            stats["mismatch"] += 1
            mismatched.append(f"block {b} string {i}: different English text")
            continue
        if len(ru) + 1 > n:
            stats["too_long"] += 1
            mismatched.append(f"block {b} string {i}: too long for its slot")
            continue
        out[a:a + 2 * n] = (ru + " " * (n - 1 - len(ru)) + "\x00").encode("utf-16-be")
        stats["pack"] += 1
    return bytes(out), stats, mismatched


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pc-ru", type=Path, help="PC english.bnl with the ZoG translation")
    ap.add_argument("--pc-en", type=Path, help="original PC english.bnl (installer backup)")
    ap.add_argument("--pack", type=Path,
                    help=f"build from a translation pack instead (default: {PACK} when --pc-ru/--pc-en are not given)")
    ap.add_argument("--export-pack", type=Path, metavar="FILE",
                    help="write the translation pack from an existing russian.bnl (--out) and exit")
    ap.add_argument("--x360", type=Path, help="Xbox english.bnl (default: english.bnl.orig or english.bnl)")
    ap.add_argument("--out", type=Path, default=BUNDLES / "russian.bnl")
    args = ap.parse_args()

    x360_path = args.x360
    if x360_path is None:
        orig = BUNDLES / "english.bnl.orig"
        x360_path = orig if orig.exists() else BUNDLES / "english.bnl"
    try:
        x360 = Caff(x360_path)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if not x360.big:
        print(f"error: {x360_path} is not an Xbox 360 file", file=sys.stderr)
        return 1
    if hashlib.sha1(x360.raw).hexdigest() != X360_ENGLISH_SHA1:
        print(f"warning: {x360_path} is not the tested english.bnl; continuing", file=sys.stderr)
    x_blocks = parse_blocks(x360.text, True)

    if args.export_pack:
        return export_pack(x360, args.out, args.export_pack)

    pack = args.pack
    if pack is None and not (args.pc_ru or args.pc_en) and PACK.exists():
        pack = PACK
    if pack is not None:
        print(f"using the translation pack {pack}")
        try:
            out, stats, untranslated = text_from_pack(pack, x_blocks, x360.text)
        except (OSError, ValueError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        if stats["mismatch"]:
            print(f"warning: {stats['mismatch']} pack strings do not match this english.bnl", file=sys.stderr)
    else:
        if not (args.pc_ru and args.pc_en):
            print(f"error: give --pc-ru and --pc-en (the PC version with the ZoG translation), "
                  f"or put the translation pack at {PACK}", file=sys.stderr)
            return 1
        try:
            pc_ru, pc_en = Caff(args.pc_ru), Caff(args.pc_en)
        except (OSError, ValueError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        if pc_ru.big or pc_en.big:
            print("error: --pc-ru and --pc-en must be PC (little-endian) files", file=sys.stderr)
            return 1
        ru_blocks = parse_blocks(pc_ru.text, False)
        en_blocks = parse_blocks(pc_en.text, False)
        if layout(ru_blocks) != layout(en_blocks):
            print("error: the two PC files do not share a layout (wrong backup?)", file=sys.stderr)
            return 1
        out, stats, untranslated = build_text(en_blocks, ru_blocks, x_blocks, x360.text)

    packed = compress_exact(out, x360.text_size)
    if packed is None:
        return 1
    result = x360.raw[:x360.text_offset] + packed

    # Self-check: same size, same layout, text decodes back.
    check = zlib.decompressobj().decompress(result[x360.text_offset:])
    if len(result) != len(x360.raw) or check != bytes(out) or \
            layout(parse_blocks(check, True)) != layout(x_blocks):
        print("error: self-check failed", file=sys.stderr)
        return 1

    args.out.write_bytes(result)
    report = args.out.with_name("russian_report.txt")
    with report.open("w", encoding="utf-8") as f:
        f.write(f"source: {x360_path}\n{dict(stats)}\n\nNot translated ({len(untranslated)}):\n")
        for s in untranslated:
            f.write(s.replace("\n", "\\n") + "\n")
    done = sum(v for k, v in stats.items() if k not in ("untranslated", "too_long", "mismatch"))
    print(f"{args.out}: {done} strings translated, {len(untranslated)} left in English "
          f"({dict(stats)}); list in {report.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
