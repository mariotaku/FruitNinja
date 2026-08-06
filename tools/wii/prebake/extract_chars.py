#!/usr/bin/env python3
# Extract per-language unique Unicode codepoints from the shipped
# translations_<lang>.str files (Mortar::StringTable language blob).
#
# Writes chars_<lang>.txt + chars_summary.json into --out. bake-fonts.py reads
# chars_<lang>.txt for every plan entry whose mode is "full" (all non-CJK
# languages at the small UI sizes), so this runs as part of the Wii bake chain.
#
# File format (little-endian), per src/engine/util/StringTable.cpp
# LoadLanguage @0x0022d6fc:
#   FileHeader wrapper (76 bytes):
#     magic          u32   (== 1)
#     token[64]      bytes (GUID)
#     blob_byte_size u32   (@0x44) -- includes the trailing count field (+4)
#     count          u32   (@0x48)
#   then raw payload of (blob_byte_size - 4) bytes:
#     StringEntry[count], each 12 bytes: str_offset u32, strlen u32, strlen2 u32
#       (str_offset is relative to the start of the string blob that follows
#        the entries; i.e. add count*12 to get the offset within the payload)
#     string blob: NUL-terminated UTF-8 strings
#
# We don't need the header file (key->idx map) -- to collect the used-glyph
# set we just decode every string in every entry.

import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths  # noqa: E402

ROOT, OUT_DIR = _paths.parse_args(
    "Extract per-language codepoint sets from the shipped string tables.")
STR_DIR = _paths.stringtables_dir(ROOT)

# (code tag, file suffix) -- full StringTable set (src/engine/util/StringTable.cpp
# kLanguageSuffix). "fake debug language" excluded from prebake (dev-only).
LANGS = [
    ("english_us",          "english_us"),
    ("english_uk",          "english_uk"),
    ("french",              "french"),
    ("spanish",             "spanish"),
    ("german",              "german"),
    ("italian",             "italian"),
    ("dutch",               "dutch"),
    ("swedish",             "swedish"),
    ("danish",              "danish"),
    ("norwegian",           "norwegian"),
    ("finnish",             "finnish"),
    ("korean",              "korean"),
    ("japanese",            "japanese"),
    ("chinese",             "chinese"),
    ("traditional_chinese", "traditional chinese"),
    ("latin_spanish",       "latin spanish"),
    ("polish",              "polish"),
    ("portuguese_pt",       "portuguese (pt)"),
    ("portuguese_br",       "portuguese (br)"),
    ("russian",             "russian"),
    ("arabic",              "arabic"),
]

def parse_lang(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 1:
        raise ValueError("bad magic %d in %s" % (magic, path))
    blob_byte_size = struct.unpack_from("<I", data, 0x44)[0]
    count          = struct.unpack_from("<I", data, 0x48)[0]
    payload_off = 76
    payload = data[payload_off: payload_off + (blob_byte_size - 4)]
    entries_size = count * 12
    strings = []
    seen_off = set()
    for i in range(count):
        str_off, slen, slen2 = struct.unpack_from("<III", payload, i * 12)
        abs_off = entries_size + str_off
        if abs_off in seen_off:
            continue        # dedup shared/empty strings
        seen_off.add(abs_off)
        end = payload.find(b"\x00", abs_off)
        if end < 0:
            end = len(payload)
        raw = payload[abs_off:end]
        try:
            strings.append(raw.decode("utf-8"))
        except UnicodeDecodeError:
            strings.append(raw.decode("utf-8", "replace"))
    return count, strings

def script_of(cp):
    if cp < 0x80: return "ASCII"
    if 0x80 <= cp <= 0x24F: return "Latin"     # Latin-1 + ext A/B (accents)
    if 0x400 <= cp <= 0x4FF: return "Cyrillic"
    if 0x600 <= cp <= 0x6FF or 0xFB50 <= cp <= 0xFEFF: return "Arabic"
    if 0xAC00 <= cp <= 0xD7A3 or 0x1100 <= cp <= 0x11FF or 0x3130 <= cp <= 0x318F: return "Hangul"
    if 0x3040 <= cp <= 0x309F: return "Hiragana"
    if 0x30A0 <= cp <= 0x30FF: return "Katakana"
    if 0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF: return "CJK"
    if 0x3000 <= cp <= 0x303F or 0xFF00 <= cp <= 0xFFEF: return "CJK-punct"
    if 0x2000 <= cp <= 0x206F or 0x2100 <= cp <= 0x21FF or 0x2500 <= cp <= 0x25FF: return "Symbols"
    return "Other"

summary = {}
for tag, suffix in LANGS:
    path = os.path.join(STR_DIR, "translations_%s.str" % suffix)
    if not os.path.exists(path):
        print("MISSING", path); continue
    count, strings = parse_lang(path)
    cps = set()
    for s in strings:
        for ch in s:
            cp = ord(ch)
            if cp in (0x0a, 0x0d, 0x09):   # newline/tab -- not glyphs
                continue
            cps.add(cp)
    cps = sorted(cps)
    # write per-language file: UTF-8 chars line, then hex codepoint list
    outp = os.path.join(OUT_DIR, "chars_%s.txt" % tag)
    with open(outp, "w", encoding="utf-8") as o:
        o.write("# lang=%s  entries=%d  unique_codepoints=%d\n" % (tag, count, len(cps)))
        o.write("# format: line1 = all glyphs as UTF-8 text; line2+ = one 'U+XXXX\\tCHAR' per cp\n")
        o.write("".join(chr(c) for c in cps) + "\n")
        for c in cps:
            o.write("U+%04X\t%s\n" % (c, chr(c)))
    scripts = {}
    for c in cps:
        s = script_of(c)
        scripts[s] = scripts.get(s, 0) + 1
    summary[tag] = {"entries": count, "unique_cp": len(cps), "scripts": scripts}
    print("%-22s entries=%-4d cps=%-5d %s" % (tag, count, len(cps), scripts))

with open(os.path.join(OUT_DIR, "chars_summary.json"), "w") as o:
    json.dump(summary, o, indent=2)
print("\nwrote chars_summary.json + chars_<lang>.txt to %s" % OUT_DIR)
