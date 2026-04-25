# Font Slots in g_GameData

<!-- Analysed: 2026-04-25T12:00 -->

## Overview

`g_GameData` (the flat 0x608-byte gameplay struct) holds 11 `Font*` slots at
offsets +0x50 through +0x80.  All populated slots are loaded in
`GameInitialise @ 0x0010bdfc`.  Three slots are reserved (zeroed but never
loaded by any Font::Load call in this binary version).  `GameDestroy @
0x0010b7ec` deletes every non-null slot.

`MainScreen` also owns a private `Font*` (loaded from `verdana.fnt`) stored as
a member of the `MainScreen` object (`m_pFont`), **not** in g_GameData.

---

## Font::Load Signature

```
// Binary: 0x00199e9c (270 lines)
// Thunk:  0x000fb960
int __thiscall Mortar::Font::Load(Font* this, const char* path);
// Returns 1 on success, 0 on failure.
// Self-contained: allocates glyphs, reads page textures via TextureManager.
// No separate Font::Init or Font::SetScale method exists in the binary.
```

Font object size: **0x438 bytes** (`operator_new(0x438)` before each call).

---

## HD vs SD Selection

`GameInitialise` calls `DisplayManager::ShouldUseHDFonts()` once and stores
the result in `useHDFonts` (a local int).  Slots +0x54 and +0x58 use a branch
on this flag to pick between HD and SD paths.  All other slots use a single
path regardless of HD flag.

---

## Per-Slot Table

| g_GameData offset | Decimal | SD filename | HD filename | Loader call site | Primary readers |
|-------------------|---------|-------------|-------------|------------------|-----------------|
| +0x50 | 80 | *(none — never loaded)* | — | n/a | *(none found)* |
| +0x54 | 84 | `fonts/font_fruit_ninja.fnt` | `fonts/font_fruit_ninja_HD.fnt` | `0x0010bf3a` | AboutScreen::Draw, ScoreControl, NotificationControl, FruitFactControl, ShopListItem, PowerUpShop, ScrollingMenuItem, LeaderboardItem, FriendLeaderboardItem, many others |
| +0x58 | 88 | `fonts/fruit_ninja_numbers.fnt` | `fonts/fruit_ninja_numbers_HD.fnt` | `0x0010bf6e` (guarded: only if +0x58 == 0) | ScoreControl, TimeControl, GameOverScreen, ScoreMultiplyerBoard, ProgressionTimerControl |
| +0x5C | 92 | *(none — never loaded)* | — | n/a | CoinCounter::Draw |
| +0x60 | 96 | *(none — gap)* | — | n/a | *(none found)* |
| +0x64 | 100 | *(none — never loaded)* | — | n/a | *(none found)* |
| +0x68 | 104 | `fonts/fruit_ninja_numbers_green.fnt` | — | `0x0010c082` (guarded: only if +0x68 == 0) | unknown |
| +0x6C | 108 | `fonts/arcade_results_numbers.fnt` | — | `0x0010bfa4` (guarded: only if +0x6C == 0) | FriendLeaderboardItem::Draw (alternate path) |
| +0x70 | 112 | `fonts/gold_numbers.fnt` | — | `0x0010bfcc` (File::Exists guard) | unknown |
| +0x74 | 116 | `fonts/silver_numbers.fnt` | — | `0x0010bff0` (File::Exists guard) | unknown |
| +0x78 | 120 | `fonts/bronze_numbers.fnt` | — | `0x0010c014` (File::Exists guard) | unknown |
| +0x7C | 124 | *(alias of +0x6C)* | — | `*(+0x7c) = *(+0x6c)` | unknown |
| +0x80 | 128 | `fonts/fruit_ninja_numbers_blue2.fnt` | — | `0x0010c038` (guarded: only if +0x80 == 0) | ScoreControl (multiplier), ScoreMultiplyerBoard, BonusScreen |

### Notes on loading guards

Slots +0x58, +0x68, +0x6C, +0x80 are loaded only when they are null
at the time GameInitialise runs:
```c
if (*(int*)(g_GameData + 0x58) == 0) {
    pFVar8 = operator_new(0x438);
    Font::Font(pFVar8);
    *(Font**)(g_GameData + 0x58) = pFVar8;
    Font::Load(pFVar8, path_sd_or_hd);
}
```
This guard allows the slots to survive an internal soft-restart (if ever
triggered) without leaking the old Font object.

### Optional medal font slots (+0x70..+0x78)

These are loaded only if `File::Exists(path)` returns true.  The .fnt files
`gold_numbers.fnt`, `silver_numbers.fnt`, `bronze_numbers.fnt` are **not
present** in the `FruitNinjaBada/Data/fonts/` directory shipped with this
binary — so these slots always stay null at runtime.

### Alias slot +0x7C

After loading +0x6C:
```c
uVar14 = *(undefined4*)(g_GameData + 0x6c);
*(undefined4*)(g_GameData + 0x7c) = uVar14;
*(undefined4*)(g_GameData + 0x70) = uVar14;   // overwritten again if gold_numbers.fnt loads
*(undefined4*)(g_GameData + 0x74) = uVar14;   // overwritten again if silver_numbers.fnt loads
*(undefined4*)(g_GameData + 0x78) = uVar14;   // overwritten again if bronze_numbers.fnt loads
```
So +0x70, +0x74, +0x78, +0x7c are all set to the arcade_results_numbers font
as a fallback, then selectively overwritten by the optional medal fonts.

GameDestroy handles +0x70..+0x7c via a loop that skips deletion if the slot
equals +0x6c (i.e. it is a non-owning alias):
```c
// Loop body (iterates +0x70, +0x74, +0x78, +0x7c):
if (slot_ptr == *(Font**)(g_GameData + 0x6c)) {
    *slot = null;      // alias — just clear, don't delete
} else if (slot_ptr != null) {
    Font::~Font(slot_ptr);
    operator_delete(slot_ptr);
    *slot = null;
}
```

### Reserved / unused slots

| Offset | Status | Evidence |
|--------|--------|----------|
| +0x50 | Reserved — always null | Zeroed in GameInitialise; deleted (with null guard) in GameDestroy; no Font::Load targets it anywhere in binary |
| +0x5C | Reserved — always null | Same pattern; CoinCounter::Draw presumably reads it but the pointer is always null in this binary |
| +0x60 | Gap — not a Font slot | Not in GameDestroy's deletion sequence; not zeroed in GameInitialise (the nearby zero block zeroes +0x50..+0x80 as a bulk memset, but +0x60 is not explicitly deleted by name) |
| +0x64 | Reserved — always null | Zeroed; deleted as `*(g_GameData + 100)`; no loader found |

---

## Port Method Requirements

The port's existing `Font::Load` (`src/engine/render/Font.cpp`) matches the
binary's `0x00199e9c` signature.  **No additional Font methods need porting**
for the font-slot system:

- `Font::Init` — does not exist as a separate function in the binary; `Font::Font` (ctor) + `Font::Load` is the complete construction sequence.
- `Font::SetScale` — not present in binary.
- `BakedString::Bake` — exists in the binary (`0x00198e44` region) but is a separate text-caching feature not yet needed by any ported screen.

---

## Font Asset Cross-Reference

All font filenames used in the binary:

| String address | Filename | g_GameData slot |
|----------------|----------|-----------------|
| 0x001b97f1 | `fonts/font_fruit_ninja_HD.fnt` | +0x54 (HD path) |
| 0x001b980f | `fonts/font_fruit_ninja.fnt` | +0x54 (SD path) |
| 0x001b982a | `fonts/fruit_ninja_numbers_HD.fnt` | +0x58 (HD path) |
| 0x001b984b | `fonts/fruit_ninja_numbers.fnt` | +0x58 (SD path) |
| 0x001b9869 | `fonts/arcade_results_numbers.fnt` | +0x6C |
| 0x001b988a | `fonts/gold_numbers.fnt` | +0x70 (optional) |
| 0x001b98a1 | `fonts/silver_numbers.fnt` | +0x74 (optional) |
| 0x001b98ba | `fonts/bronze_numbers.fnt` | +0x78 (optional) |
| 0x001b98d3 | `fonts/fruit_ninja_numbers_blue2.fnt` | +0x80 |
| 0x001b98f7 | `fonts/fruit_ninja_numbers_green.fnt` | +0x68 |
| 0x001bbcb3 | `fonts/verdana.fnt` | MainScreen::m_pFont (NOT g_GameData) |

Files in `FruitNinjaBada/Data/fonts/` that are **NOT referenced** by any
string in the binary: `electrofied_medium.fnt`, `fruit_ninja_numbers_blue.fnt`,
`fruit_ninja_numbers_red.fnt`, `fruit_ninja_numbers_silver.fnt`.

---

## See Also

- `docs/engine/formats/fonts.md` — BMFont .fnt file format
- `docs/engine/rendering-detail.md` — Font::Load, DrawString, QUADCUSTOMVERTEX
- `docs/structs/game.md` — g_GameData full layout (+0x50..+0x80 region)
