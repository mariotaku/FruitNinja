#!/usr/bin/env python3
# Collect the set of localisation KEYS the game actually references, from two sources:
#   1) Code literals + LSTR_* IDs (hardcoded list below, from the src/ reference audit).
#   2) Data XML files that cite table keys by name (achievements, fruit facts,
#      shop items, bonuses, powerups, etc.) -- any token in an XML that also
#      exists as a header key is treated as USED (conservative).
# Then emit used_keys.txt (sorted) + used_sets.json (the resolved str_idx set),
# which build_plan.py consumes to prune the CJK small-size charsets (#55).

import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths  # noqa: E402

ROOT, OUT = _paths.parse_args(
    "Resolve the localisation keys/string indices the game actually references.")
STR = _paths.stringtables_dir(ROOT)
XML = _paths.xml_dir(ROOT)

def parse_header(p):
    d = open(p, "rb").read()
    bsize = struct.unpack_from("<I", d, 0x44)[0]
    count = struct.unpack_from("<I", d, 0x48)[0]
    raw = d[76:76 + (bsize - 4)]
    esz = count * 40
    kb = raw[esz:]
    keys = []; sidx = []
    for i in range(count):
        e = struct.unpack_from("<10I", raw, i * 40)
        end = kb.find(b"\x00", e[0])
        keys.append(kb[e[0]:end].decode("utf-8", "replace"))
        sidx.append(e[9])
    return keys, sidx

keys, sidx = parse_header(os.path.join(STR, "translations_header.str"))
key_set = set(keys)
key_to_idx = {k: i for i, k in enumerate(keys)}   # header row index (== LSTR id)

# ---- 1. Code-referenced literal IDs (from src/ audit) ------------------------
# Named LSTR_ + hex-cast IDs actively referenced. These are header ROW indices.
CODE_IDS = [
    0xab,0xae,0xc4,0x15d,0x2ef,0xc8,0xc9,0xca,0xcb,0x12f,0xce,0xcf,0xd7,0xd8,
    0x397,0x398,0x39c,0x39d,0x412,0x7b,0x363,0x349,0x347,0x348,0x34a,0x34b,0x34c,
    0x34d,0x34e,0x34f,0x350,0x3c3,0x323,0x352,0x3c2,0x11e,0x11f,0x379,0x37a,0x37b,
    0x3c8,0x35f,0x2dc,0x399,0x3c5,
    0xed,0x3c7,0x111,0x3ba,0x39f,0x3be,0x3bf,0x3c0,0x3b5,0x2db,0x2f9,0x31e,0x31f,
]
# FruitFactCombo::GetComboStarText iterates GAME_TEXTURE_55..85 = IDs 0x324..0x342
# (31 combo-star labels), src/hud/FruitFactCombo.cpp:191. Whole range is USED.
CODE_IDS += list(range(0x324, 0x342 + 1))

used_keys = set()
for i in CODE_IDS:
    if 0 <= i < len(keys):
        used_keys.add(keys[i])

# ---- 2. XML-referenced keys --------------------------------------------------
# Scan every XML for tokens that are header keys. This captures achievement
# names/descs, fruit facts, shop item titles/descs, bonus templates, etc.
TOKEN = re.compile(r"[A-Za-z][A-Za-z0-9_]+")
xml_hits = {}
for fn in sorted(os.listdir(XML)):
    if not fn.endswith(".xml"):
        continue
    txt = open(os.path.join(XML, fn), encoding="utf-8", errors="replace").read()
    hits = set(t for t in TOKEN.findall(txt) if t in key_set)
    if hits:
        xml_hits[fn] = sorted(hits)
        used_keys |= hits

# ---- 3. Derived/implied keys -------------------------------------------------
# ACHIEVEMENT_DESC_XX + LITE_ACHIEVEMENT_DESC_XX are built from the achievement
# "name" numeric suffix at runtime (AchievementManager.cpp). For every
# ACHIEVEMENT_NN name we see, the code also fetches ACHIEVEMENT_DESC_NN and
# (lite) LITE_ACHIEVEMENT_DESC_NN. Add those that exist in the table.
extra = set()
for k in list(used_keys):
    m = re.match(r"ACHIEVEMENT_(\d\d)$", k)
    if m:
        for cand in ("ACHIEVEMENT_DESC_" + m.group(1),
                     "LITE_ACHIEVEMENT_DESC_" + m.group(1),
                     "ACHIEVEMENT_Post_DESC_" + m.group(1)):
            if cand in key_set:
                extra.add(cand)
used_keys |= extra

# Resolve to str_idx set + header-row (LSTR) set
used_rows = sorted(key_to_idx[k] for k in used_keys)
used_sidx = sorted(set(sidx[key_to_idx[k]] for k in used_keys))

out = {
    "header_key_count": len(keys),
    "used_key_count": len(used_keys),
    "dead_key_count": len(keys) - len(used_keys),
    "code_id_count": len(CODE_IDS),
    "xml_files_with_hits": {k: len(v) for k, v in xml_hits.items()},
    "derived_desc_keys": len(extra),
}
json.dump(out, open(os.path.join(OUT, "used_keys_meta.json"), "w"), indent=1)

with open(os.path.join(OUT, "used_keys.txt"), "w", encoding="utf-8") as o:
    for k in sorted(used_keys):
        o.write("%s\t%d\n" % (k, key_to_idx[k]))

with open(os.path.join(OUT, "dead_keys.txt"), "w", encoding="utf-8") as o:
    for i, k in enumerate(keys):
        if k not in used_keys:
            o.write("%s\t%d\n" % (k, i))

json.dump({"used_rows": used_rows, "used_sidx": used_sidx},
          open(os.path.join(OUT, "used_sets.json"), "w"))

print("header keys        :", len(keys))
print("USED keys          :", len(used_keys))
print("DEAD keys          :", len(keys) - len(used_keys))
print("used str_idx (uniq):", len(used_sidx))
print("\nXML files contributing keys:")
for k, v in sorted(xml_hits.items(), key=lambda kv: -len(kv[1])):
    print("  %-28s %d keys" % (k, len(v)))
print("\nderived DESC keys added:", len(extra))
