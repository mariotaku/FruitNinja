#!/usr/bin/env python3
# Build the usage-pruned Wii font prebake plan (#51).
# Strategy: small UI sizes -> FULL per-language charset; large sizes -> SUBSET
# (only glyphs from the specific strings that render at that size).
#
# Small-size FULL bake covers MenuButton runtime-shrunk fontScale (<= base 10/12):
# runtime never bakes larger than base, so it downscales the baked glyph.
#
# Reads used_sets.json from --out (written by collect_used.py, run it first) and
# writes bake_plan.json + footprint.json there. bake-fonts.py then consumes that
# same directory (bake_plan.json + the chars_<lang>.txt extract_chars.py wrote).

import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths  # noqa: E402

ROOT, OUT = _paths.parse_args("Build the Wii font prebake plan (bake_plan.json).")
STR = _paths.stringtables_dir(ROOT)

# ---- StringTable parsers (mirror src/engine/util/StringTable.cpp) -------------
def parse_header(p):
    d = open(p, "rb").read()
    bsize = struct.unpack_from("<I", d, 0x44)[0]
    count = struct.unpack_from("<I", d, 0x48)[0]
    raw = d[76:76 + (bsize - 4)]
    esz = count * 40
    kb = raw[esz:]
    out = []
    for i in range(count):
        e = struct.unpack_from("<10I", raw, i * 40)
        end = kb.find(b"\x00", e[0])
        out.append((kb[e[0]:end].decode("utf-8", "replace"), e[9]))  # (keyname, str_idx)
    return out

def parse_lang(p):
    d = open(p, "rb").read()
    bsize = struct.unpack_from("<I", d, 0x44)[0]
    count = struct.unpack_from("<I", d, 0x48)[0]
    payload = d[76:76 + (bsize - 4)]
    esz = count * 12
    out = []
    for i in range(count):
        off, sl, sl2 = struct.unpack_from("<III", payload, i * 12)
        a = esz + off
        e = payload.find(b"\x00", a)
        if e < 0:
            e = len(payload)
        out.append(payload[a:e].decode("utf-8", "replace"))
    return out

# Language tag -> shipped .str suffix. dutch/swedish/danish/norwegian/finnish
# fall back to english_us (no dedicated table) -> not baked separately.
LANGS = [
    ("english_us", "english_us"), ("english_uk", "english_uk"), ("french", "french"),
    ("spanish", "spanish"), ("german", "german"), ("italian", "italian"),
    ("korean", "korean"), ("japanese", "japanese"), ("chinese", "chinese"),
    ("traditional_chinese", "traditional chinese"), ("latin_spanish", "latin spanish"),
    ("polish", "polish"), ("portuguese_pt", "portuguese (pt)"),
    ("portuguese_br", "portuguese (br)"), ("russian", "russian"), ("arabic", "arabic"),
]

H = parse_header(os.path.join(STR, "translations_header.str"))
def lang_strings(suffix):
    return parse_lang(os.path.join(STR, "translations_%s.str" % suffix))

# ---- Used-string prune (#55) -------------------------------------------------
# Baking every glyph in the string table wastes atlas RAM on strings the v1.6.1
# Bada build never displays (LITE_* free-SKU, WindowsPhone/Android/arcade relics,
# unused fruit-facts). collect_used.py resolves the ACTUALLY-referenced string
# indices (code IDs + XML key refs + derived DESC keys) into used_sets.json.
# We prune ONLY the CJK languages' small-size charset -- that's where the glyph
# count explodes (36-41% cut) and the savings are real. Latin/Cyrillic/Arabic
# stay FULL (tiny already, no point risking a blank glyph). ASCII printable is
# always re-added downstream by the baker's glyph_set_for, so numeric/Latin
# runtime text (e.g. "%i HITS") never blanks even in a pruned CJK atlas.
CJK_LANGS = set(("korean", "japanese", "chinese", "traditional_chinese"))
_used_path = os.path.join(OUT, "used_sets.json")
USED_SIDX = None
if os.path.exists(_used_path):
    USED_SIDX = set(json.load(open(_used_path))["used_sidx"])
else:
    print("WARN: used_sets.json missing -- run collect_used.py first; baking FULL (no prune)")

def used_cps(strings):
    """Glyph union over only the USED strings (str_idx in USED_SIDX)."""
    s = set()
    for i, st in enumerate(strings):
        if i not in USED_SIDX:
            continue
        for ch in st:
            cp = ord(ch)
            if cp in SKIP_CP:
                continue
            s.add(cp)
    return s

# ---- Full per-language codepoint sets (recompute; matches chars_<lang>.txt) ---
SKIP_CP = set((0x0a, 0x0d, 0x09))
def full_cps(strings):
    s = set()
    for st in strings:
        for ch in st:
            cp = ord(ch)
            if cp in SKIP_CP:
                continue
            s.add(cp)
    return s

# ---- Canonical sizes + snap map ----------------------------------------------
CANONICAL = [10, 12, 14, 16, 20, 22, 30, 50, 56]
SNAP = {8: 10, 9: 10, 9.9: 10, 10: 10, 12: 12, 13: 14, 14: 14, 16: 16, 17: 16,
        20: 20, 22: 22, 30: 30, 50: 50, 56: 56}
SMALL_SIZES = [10, 12, 14, 16]      # FULL charset per language
LARGE_SIZES = [20, 22, 30, 50, 56]  # SUBSET

# ---- Large-size string sets: LocalizedString id (== header row index) --------
# Each id resolves H[id].str_idx -> language string.
LARGE_IDS = {
    20: [0x2DC, 0x3C3, 0x349, 0x31F],   # NEW BEST!, ABOUT title, CREDITS heading, TOTAL
    22: [0x3BA, 0x39F],                 # MODE SELECT, MULTIPLAYER
    30: [0x397, 0x31E, 0x323],          # DOJO, BONUS, SCORE
    50: [],                             # combo popup: ASCII literals only (below)
    56: [0x2DB, 0x2F9],                 # GAME OVER, TIME UP
}
# ASCII literal strings rendered at a large size (all langs identical glyphs).
LARGE_LITERALS = {
    50: ["SLICE!", "1 HIT", "0123456789 HITS", "+0123456789"],  # combo/score popup
}

# Titles use FitStringToWidth, which SHRINKS the base size DOWN to fit the box
# (never grows). A base-size string therefore renders at ANY smaller canonical
# large size when the localized text is wide (e.g. JP "ゲームオーバー" is wider than
# "GAME OVER", so the size-56 GameOver title shrinks to 50). On Wii a missing
# glyph = invisible text (no stb fallback), so every LARGE_IDS string must be
# baked at its base size AND all smaller large sizes. Propagate each size's ids
# downward into every smaller large size (was: GAME OVER only in size 56 -> the
# shrunk-to-50 JP title had no katakana -> title didn't draw).
_large_desc = sorted(LARGE_SIZES, reverse=True)   # [56, 50, 30, 22, 20]
for _i in range(len(_large_desc)):
    _sz = _large_desc[_i]
    for _smaller in _large_desc[_i + 1:]:
        merged = set(LARGE_IDS.get(_smaller, [])) | set(LARGE_IDS.get(_sz, []))
        LARGE_IDS[_smaller] = sorted(merged)

def cps_of_str(s):
    return set(ord(c) for c in s if ord(c) not in SKIP_CP)

# ---- Build the plan ----------------------------------------------------------
plan = {}
lang_full = {}
for tag, suffix in LANGS:
    L = lang_strings(suffix)
    fset = full_cps(L)
    # CJK small-size charset is pruned to used strings (#55); non-CJK stays full.
    prune = tag in CJK_LANGS and USED_SIDX is not None
    small_set = used_cps(L) if prune else fset
    lang_full[tag] = small_set   # the set actually baked at small sizes (drives footprint + is_cjk)
    entry = {}
    for sz in SMALL_SIZES:
        if prune:
            # Emit as an explicit subset so the baker reads these codepoints from
            # the plan (its "full" mode would otherwise re-read chars_<lang>.txt).
            entry[str(sz)] = {"mode": "subset", "codepoints": sorted(small_set),
                               "count": len(small_set), "pruned": True}
        else:
            entry[str(sz)] = {"mode": "full", "count": len(fset)}
    for sz in LARGE_SIZES:
        cps = set()
        for hid in LARGE_IDS[sz]:
            keyname, sidx = H[hid]
            if sidx < len(L):
                cps |= cps_of_str(L[sidx])
        for lit in LARGE_LITERALS.get(sz, []):
            cps |= cps_of_str(lit)
        entry[str(sz)] = {"mode": "subset", "codepoints": sorted(cps), "count": len(cps)}
    plan[tag] = entry

out = {
    "_doc": "Wii CJK glyph prebake plan (#51/#55). Usage-pruned: NON-CJK small UI sizes bake the FULL per-language charset; CJK small sizes bake only glyphs from ACTUALLY-USED strings (#55, used_sets.json -- 36-41% fewer CJK glyphs); large sizes bake only glyphs from the specific strings that render there. ASCII printable (0x20-0x7E) is always re-added by the baker so numeric/Latin text never blanks. Small-size bake also covers MenuButton runtime-shrunk fontScale (runtime downscales base glyph). kFontSupersample=1 on Wii => sizes are literal px. Atlas = IA8 (2B/texel), 512x512 GX-tiled pages.",
    "canonical_sizes": CANONICAL,
    "snap_map": {str(k): v for k, v in SNAP.items()},
    "small_sizes_full": SMALL_SIZES,
    "large_sizes_subset": LARGE_SIZES,
    "large_size_strings": {
        str(sz): {"loc_ids": ["0x%X" % i for i in LARGE_IDS[sz]],
                  "loc_keys": [H[i][0] for i in LARGE_IDS[sz]],
                  "ascii_literals": LARGE_LITERALS.get(sz, [])}
        for sz in LARGE_SIZES},
    "plan": plan,
    "full_charset_counts": {t: len(s) for t, s in lang_full.items()},
}
json.dump(out, open(os.path.join(OUT, "bake_plan.json"), "w"), indent=1)
print("wrote %s  (%d langs)" % (os.path.join(OUT, "bake_plan.json"), len(plan)))

# ---- Footprint estimate ------------------------------------------------------
def cell_dims(size, cjk):
    w = size * (1.0 if cjk else 0.62)
    h = size * 1.30
    return max(int(math.ceil(w / 4.) * 4), 4), max(int(math.ceil(h / 4.) * 4), 4)

def pack_atlas(nglyphs, size, cjk):
    # smallest power-of-two square page (<=512) that fits nglyphs; else N x 512.
    cw, ch = cell_dims(size, cjk)
    for dim in (64, 128, 256, 512):
        cols = dim // cw
        rows = dim // ch
        if cols * rows >= nglyphs and cols > 0 and rows > 0:
            return dim, 1
    cols = 512 // cw
    rows = 512 // ch
    per = max(cols * rows, 1)
    return 512, int(math.ceil(nglyphs / float(per)))

def is_cjk(tag):
    for cp in lang_full[tag]:
        if (0x3040 <= cp <= 0x30FF or 0x3400 <= cp <= 0x9FFF or 0xAC00 <= cp <= 0xD7A3
                or 0x1100 <= cp <= 0x11FF or 0x3130 <= cp <= 0x318F):
            return True
    return False

def page_bytes(dim):
    return dim * dim * 2  # IA8, 2 bytes/texel

print("\n%-20s %6s cjk | on-disc  | biggest-size pages" % ("lang", "full"))
print("-" * 74)
report = {}
grand = 0.0
maxlang = 0.0
rows = []
for tag, suffix in LANGS:
    L = lang_full[tag]
    n = len(L)
    cjk = is_cjk(tag)
    lb = 0
    detail = {}
    for sz in CANONICAL:
        ent = plan[tag][str(sz)]
        g = n if ent["mode"] == "full" else ent["count"]
        if g == 0:
            detail[sz] = {"glyphs": 0, "page": 0, "pages": 0, "bytes": 0}
            continue
        dim, p = pack_atlas(g, sz, cjk)
        by = page_bytes(dim) * p
        lb += by
        detail[sz] = {"glyphs": g, "page": dim, "pages": p, "bytes": by}
    grand += lb
    maxlang = max(maxlang, lb)
    report[tag] = {"full": n, "cjk": cjk, "disc_bytes": lb, "detail": detail}
    rows.append((tag, n, cjk, lb, detail))

rows.sort(key=lambda r: -r[3])
for tag, n, cjk, lb, detail in rows:
    big = max(detail.items(), key=lambda kv: kv[1]["bytes"])
    sys.stdout.write("%-20s %6d %3s | %6.2f MB | size%d=%dpg@%d(%dglyph)\n" % (
        tag, n, "CJK" if cjk else "lat", lb / 1024. / 1024.,
        big[0], big[1]["pages"], big[1].get("page", 0), big[1]["glyphs"]))
print("-" * 74)
print("TOTAL on-disc (all baked langs, all sizes): %.2f MB" % (grand / 1024. / 1024.))
print("Single-active-language resident UPPER BOUND: %.2f MB (heaviest lang, all sizes)" % (maxlang / 1024. / 1024.))

print("\nLarge-size SUBSET glyph counts (max over langs):")
for sz in LARGE_SIZES:
    mx = max(plan[t][str(sz)]["count"] for t, _ in LANGS)
    ex = plan["chinese"][str(sz)]["count"]
    sys.stdout.write("  size %2d: max=%d glyphs (chinese=%d)\n" % (sz, mx, ex))

json.dump(report, open(os.path.join(OUT, "footprint.json"), "w"), indent=1)
