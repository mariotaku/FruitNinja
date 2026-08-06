# Wii font prebake plan generators

The Wii build links no runtime TTF backend, so every glyph comes from the
offline atlases `../bake-fonts.py` writes. This directory holds the generators
that decide *which* glyphs to bake. They are build inputs, not scratch -- the
CMake Wii target runs all three before the baker.

| Script | Reads | Writes |
|--------|-------|--------|
| `extract_chars.py` | `FruitNinjaBada/Data/stringtables` | `chars_<lang>.txt`, `chars_summary.json` |
| `collect_used.py`  | `FruitNinjaBada/Data/{stringtables,xml}` + a code-audit ID list | `used_sets.json`, `used_keys.txt`, `dead_keys.txt`, `used_keys_meta.json` |
| `build_plan.py`    | `FruitNinjaBada/Data/stringtables` + `used_sets.json` | `bake_plan.json`, `footprint.json` |

Run order: `extract_chars.py` -> `collect_used.py` -> `build_plan.py`.
`bake-fonts.py` then reads `bake_plan.json` plus the `chars_<lang>.txt` files
from that same directory (its `"full"`-mode entries).

Shared options (see `_paths.py`):

    --root <dir>   repo root (default: nearest ancestor with CMakeLists.txt + tools/wii)
    --out <dir>    output directory (default: <root>/tmp/prebake)

The CMake build passes `--out ${CMAKE_BINARY_DIR}/prebake` so a fresh checkout
regenerates the plan instead of depending on the gitignored `tmp/prebake`. The
atlas format itself is documented in `../prebaked-font-format.md`.
