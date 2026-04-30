# UI Widget Classes

Recovered from FruitNinja.exe (ARM32 Bada) via Ghidra decompilation.
All classes inherit from HUDControl3d (which inherits HUDControl) unless noted.

Base HUDControl layout (offsets 0x00-0x78):
- 0x00: vtable ptr
- 0x04: unknown
- 0x08: pos_x (float)
- 0x0C: pos_y (float)
- 0x10: pos_z (float)
- 0x14-0x1C: unknown
- 0x20: size_x (float)
- 0x24: size_y (float)
- 0x28: size_z (float)
- 0x2C: m_Scale (float)
- 0x30: m_bNoDestructor (bool)
- 0x34: m_LayerFlags (uint)
- 0x38-0x3C: unknown
- 0x4C: m_Timer (float)
- 0x5C-0x60: colour area
- 0x74: m_PauseTitleTex (SmartPtr<Texture>)

---

## 1. FruitFactControl

**Constructors:** 0x0013cb60, 0x0013cc98  
**Update:** 0x0013b604 (157 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0x204 (highest field: field_0x200)

Displays fruit facts, leaderboard, and bonus screen after game over. Has navigation (left/right/up/down buttons) and online provider integration.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x78 | HUDControl3d | super | Base class |
| 0x7C | float | m_AnimTimer | Incremented by dt * 8.0, wraps at 8.0 |
| 0x80 | int | m_PageIndex | Current page/state (init 0) |
| 0x84 | int | m_PrevPage | Previous page index (init -1) |
| 0x88 | int | m_SelectedItem | Selection index (init -1) |
| 0x8C | SmartPtr<Texture> | m_ContentTex | Content texture |
| 0x9C | Colour | m_TextColour | Text colour (0x74,0x5D,0x3B,0xFF = brown) |
| 0xD0 | int | m_FactIndex | Current fact index |
| 0xD4 | float | m_FactTimer | Timer for fact display cycling |
| 0xD8 | int | field_0xD8 | Unknown |
| 0xDC | SmartPtr<Texture> | m_ExtraTex | Extra texture (set null initially) |
| 0xE4 | int | m_ViewMode | 0=bonus screen, 1=leaderboard |
| 0xE8 | LeaderboardList* | m_LeaderboardList | Allocated on demand (size=300) |
| 0xEC | int* | m_SecondaryList | Secondary list ptr |
| 0xF0 | int | field_0xF0 | Init 0 |
| 0xF4 | float | field_0xF4 | Init 0.0 |
| 0xF8 | int | m_ProviderState | 1=offline, 2=online |
| 0xFC | int | field_0xFC | Init 0 |
| 0x100 | int | field_0x100 | Init 0 |
| 0x104 | FNHighscore | m_Highscore1 | Size ~0x54 each |
| 0x158 | FNHighscore | m_Highscore2 | |
| 0x1AC | FNHighscore | m_Highscore3 | |
| 0x200 | byte | m_OnlineFlag | From provider config |

---

## 2. ScrollingMenu

**Constructors:** 0x0015b2e0, 0x0015b3b0  
**Update:** 0x0015b744 (377 lines)  
**Base class:** HUDControl (NOT HUDControl3d)  
**Estimated struct size:** ~0x100 (highest field: field106_0xfc)

Scrollable menu with touch-based scrolling. Tracks touch input for drag/swipe, manages a vector of ScrollingMenuItem pointers.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x6C | HUDControl | super | Base class (no 3d) |
| 0x70 | vector<ScrollingMenuItem*> | m_Items | Item list (12 bytes: begin/end/capacity) |
| 0x74 | int | m_TouchId | Touch ID (-1 = none) |
| 0x78 | float | m_TouchStartX | Touch start position X |
| 0x7C | float | m_TouchStartY | Touch start position Y |
| 0x80 | float | field_0x80 | Touch tracking |
| 0x84 | float | field_0x84 | |
| 0x88 | float | field_0x88 | |
| 0x9C | float | m_Width | Menu width |
| 0xA0 | float | m_Height | Menu height |
| 0xA4 | float | m_ItemHeight | Per-item height |
| 0xA8 | float | m_MinScroll | Min scroll bound |
| 0xAC | float | m_MaxScroll | Max scroll bound |
| 0xBC | int | field_0xBC | Init 0 |
| 0xC0 | int | m_SelectedIdx | Selected item (-1 = none) |
| 0xC4 | float | m_ScrollScale | Init 1.0 (0x3f800000) |
| 0xC8 | byte | field_0xC8 | Init 0 |
| 0xC9 | byte | m_TouchProcessed | Cleared each Update |
| 0xCA | byte | field_0xCA | Init 1 |
| 0xCC | int | m_CollideResult | Collide() return value |
| 0xD4 | float | field_0xD4 | Scroll position tracking |
| 0xD8 | float | field_0xD8 | |
| 0xE0-0xEC | float[4] | m_TouchRegion | Touch bounding box |
| 0xF0-0xFC | float[4] | m_ScrollRegion | Scroll range bounds |

---

## 3. ScrollingMenuItem

<!-- Analysed: 2026-04-25T23:30 -->

**Constructors:** 0x0015b194 (5 params), 0x0015b228 (5 params alt), 0x0015b5dc (0 params), 0x0015b678 (0 params)
**Subclasses:** ShopListItem (vtable @ 0x001ea030), LeaderboardItem (vtable @ 0x001e9890), FriendLeaderboardItem (vtable @ 0x001e93e0)
**Base class:** None (plain struct with vtable)
**Verified struct size:** 88 bytes (0x58) — Ghidra struct confirmed

Individual menu item with text, colour, position, and a callback delegate.
Managed by ScrollingMenu (HUDControl subclass with GetType=8).

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00 | void* | vtable | |
| 0x04 | Vec3 (float[3]) | pos | World position (set by Move vtable slot 6) |
| 0x10 | ScrollingMenu* | m_Parent | Set by SetParent (slot 8) @ 0x0015aeb4 |
| 0x14 | Colour (uint32) | m_Colour | Text/display colour |
| 0x18 | float | m_Size.x | **NO binary symbol name.** All ctors init from global Vec3 ptr. FriendLeaderboardItem ctor @ 0x0013d210 sets explicitly as scaled Vec3. ShopListItem::Move @ 0x0015d1fc reads as X sub-icon offset (adds 35.2f). |
| 0x1C | float | m_Size.y | See m_Size.x. Not observed read back after ctor in non-FriendLeaderboardItem code. |
| 0x20 | float | m_Size.z | See m_Size.x. |
| 0x24 | float | **m_Height** | **BINARY NAME** (Ghidra demangled). GetHeight()/SetHeight() target this. ROW PITCH for ScrollingMenu::Update layout. Default 25.0f. ShopListItem: 80.0f. FriendLeaderboardItem: 47.0f (DAT_0013d2f0 = 0x423c0000). |
| 0x28 | float | **m_Width** | **BINARY NAME** (Ghidra demangled). GetWidth()/SetWidth() target this. Default 0.0f. ShopListItem: 290.0f (divider span). |
| 0x2C | Delegate1\<void,ScrollingMenuItem*\> | m_ClickedFocusdCallback | 40-byte delegate block. Byte at +0x2D = m_bOnscreen (SetOnscreen slot 9 writes here). std::function at +0x30. |
| 0x54 | char* | m_Text | SetText slot 10 @ 0x0015b124 writes here. |
| 0x58+ | — | (subclass fields) | Base ends at 0x57. ShopListItem, LeaderboardItem, FriendLeaderboardItem each extend beyond 0x58. |

**Vtable layout** (ScrollingMenuItem base vtable @ 0x001e9f00):

| Slot | Offset | Function | Address |
|------|--------|----------|---------|
| 0 | +0x00 | ~dtor1 | 0x0015c3ac |
| 1 | +0x04 | ~dtor2 | 0x0015c3e8 |
| 2 | +0x08 | GetHeight() | 0x0013cdf0 — returns m_Height (+0x24) |
| 3 | +0x0C | GetWidth() | 0x0013cdf8 — returns m_Width (+0x28) |
| 4 | +0x10 | SetHeight(float) | 0x0013ce00 — writes m_Height (+0x24) |
| 5 | +0x14 | SetWidth(float) | 0x0013ce08 — writes m_Width (+0x28) |
| 6 | +0x18 | Move(Vec3) | 0x0015aea8 base; ShopListItem override 0x0015d1fc |
| 7 | +0x1C | Remove() | 0x0013d14c |
| 8 | +0x20 | SetParent(ScrollingMenu*) | 0x0015aeb4 |
| 9 | +0x24 | SetOnscreen(bool) | 0x0013ce10 — writes byte at +0x2D |
| 10 | +0x28 | SetText(char*) | 0x0015b124 |
| 11 | +0x2C | Draw() | 0x0015b480 base; ShopListItem override 0x0015eb00 |
| 12 | +0x30 | (cancel-tap signal) | 0x00147970 no-op |
| 13 | +0x34 | CollideWithButton(int) | 0x00147974 returns nullptr; FriendLeaderboardItem overrides |
| 14 | +0x38 | (touch-release signal) | 0x00147978 no-op |

**5-param ctor note:** `(float width, float height, ...)` assigns `this->m_Height = width` and `this->m_Width = height` — the parameter names are swapped vs the field names. This is the binary's own naming, not a port bug.

---

## 4. SliderControl

**Constructors:** 0x00160268 (8 params), 0x00160398 (8 params)  
**Update:** 0x00160090 (35 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0xC0

Touch-based slider widget. Detects touch in a calculated region based on texture dimensions, calls UpdateTouchPosition on drag.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x78 | HUDControl3d | super | Base (pos/size set from params) |
| 0x7C | long | m_MinValue | Slider minimum |
| 0x80 | long | m_MaxValue | Slider maximum |
| 0x84 | ushort | m_FontSize | Font size |
| 0x88 | long | m_Step | Step size |
| 0x8C | float | m_HitWidthHalf | tex_width * scale_x |
| 0x90 | float | m_HitHeightHalf | tex_height * scale_y |
| 0x94 | float | m_HitWidth2Half | tex_width * scale2_x |
| 0x98 | float | m_HitHeight2Half | tex_height * scale2_y |
| 0x9C | Utf8StringIterator | m_Label | Label text (from param_3) |
| 0xB8 | int | m_TouchId | Touch ID (-1 = none) |

**Update behavior:** Checks touch in region bounded by `(pos_x +/- hitWidthHalf, pos_y +/- hitHeight2Half)`. On touch down (state 2), starts tracking. On touch release, stops. While tracking, calls UpdateTouchPosition().

---

## 5. SpeedControl

**Constructors:** 0x0016133c, 0x00161444  
**Update:** 0x00160dc4 (202 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0xAC

Displays combo/speed gauge during gameplay. Loads a localised texture and shows particle effects when speed increases. Tracks combo bonus progression from WaveManager.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x74 | HUDControl3d | super | Base class |
| 0x74 | SmartPtr<Texture> | m_SpeedTex | Speed gauge texture |
| 0x7C | ushort | m_SpeedValue | Current speed (init 0) |
| 0x80 | float | m_ComboTimer | Combo timer / progression |
| 0x84 | float | m_Alpha | Visual alpha (init 0.0) |
| 0x88-0x90 | Vec3 | m_OrigSize | Copy of size at init |
| 0x94 | float | m_ScaleTarget | Scale multiplier (init 1.0) |
| 0x98 | float | m_CurrentScale | Current interpolated scale |
| 0x9C | int | field_0x9C | Init 0 |
| 0xA0 | PSPParticleEmitter* | m_ParticleEmitter | Particle effect (cleared when speed=0) |
| 0xA8 | float | m_FadeTimer | Fade animation timer |

**Update behavior:** Reads WaveManager combo bonus progression. Animates speed gauge scaling based on combo progress. Spawns particles on combo increase, clears on reset. Speed value increases with `dt * comboTimer * 0.25 * rate`. Colour flashes on combo changes.

---

## 6. TimeControl

**Constructors:** 0x0016221c, 0x001622e8  
**Update:** 0x001624a4 (173 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0xC8+ (field at 0xC0, char buffer at ~0xC8)

Countdown timer HUD for timed game modes. Shows remaining time with colour-coded warnings and tick sounds.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x74 | HUDControl3d | super | Base class |
| 0x74 | SmartPtr<Texture> | m_TimerTex | Timer texture |
| 0x7C | float | m_TimeRemaining | Seconds left (decremented by dt) |
| 0x80-0x88 | Vec3 | m_ScaleVec | Scale vector for display |
| 0x5C | Colour | m_TimerColour | Colour (flashes red < 11s) |
| 0xC0 | float | m_InitialTime | Initial time (-1.0 = not set) |
| 0xC8 | char[64] | m_TimeString | Formatted time string (via OS_SPrintf) |

**Update behavior:** Only active in timed game mode (IsTimedGame). Decrements m_TimeRemaining by dt (multiplied by PowerUpManager speed factor if powers enabled). Shows freeze time from PowerUpManager. Flashes colour between normal and red at different rates: `<3s` = 8Hz, `<6s` = 4Hz, `<11s` = 2Hz. Calls GameOver when time reaches 0. Plays tick sound effects alternating between two SFX.

---

## 7. TutorialControl

**Constructors:** 0x001636f8, 0x001637a0  
**Update:** 0x00163014 (101 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0x98+ (accesses past 0x90)

Shows tutorial/instruction overlays. Loads two localised textures. Animates in/out with timed phases.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x74 | HUDControl3d | super | Base (m_PauseTitleTex = first tex) |
| 0x7C | float | m_AnimTimer | Animation timer (resets to -10.0) |
| 0x80 | float | m_ScaleX | Animated scale X |
| 0x84 | float | m_ScaleY | Animated scale Y (includes +20 offset) |
| 0x88 | float | m_ScaleZ | Animated scale Z |
| 0x8C | SmartPtr<Texture> | m_SecondTex | Second tutorial texture |
| 0x90 | Colour | m_Colour | Display colour (fades alpha) |

**Update behavior:** LayerFlags = 8. Checks CanShowTute(). Multi-phase animation: phase 0 (t < 0.5): scale up from (-0.5, offset) with lerp. After 2.75s, resets timer to -10.0 (hides). Scale is multiplied by a flip factor and offset by base position. Supports horizontal flip flag.

---

## 8. ScoreControl

**Constructors:** 0x00158c7c, 0x00158d4c  
**Update:** 0x0015853c (335 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0x100 (highest field: 0xFC)

Main score display HUD. Handles score counting animation, multiplier display, new highscore detection, and blade scoring digits.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x78 | HUDControl3d | super | Base class |
| 0x7C | byte | m_Dirty | Score changed flag |
| 0x7E | ushort | m_SinWobble | Sine wobble index for animation |
| 0x80 | float | m_DisplayScore | Displayed score (interpolated) |
| 0x84 | int | m_TargetScore | Target score (integer) |
| 0x88 | int | field_0x88 | Init 0 |
| 0x8C | float | field_0x8C | Init -1.0 |
| 0x90 | float | m_ScaleMultiplier | Display scale (1.0-2.0 based on combo) |
| 0xA0 | SmartPtr<Texture> | m_ScoreTex | Score digit texture |
| 0xA4 | SmartPtr<Texture> | m_ScoreTex2 | Alternate score texture |
| 0xA8 | float | field_0xA8 | Init -2.0 |
| 0xB0 | int | m_DigitCount | Number of visible digits |
| 0xB4 | int | m_PrevComboId | Previous combo identifier |
| 0xB8 | float[16] | m_DigitAlpha | Per-digit alpha (array of 16 floats) |
| 0xF8 | SmartPtr<Texture> | m_HighscoreTex | Highscore banner texture |
| 0xFC | int | m_PlayerIndex | Player index (0=single, for multiplayer) |

**Update behavior:** Gets current score, clamps digit count to 15. Animates each digit's alpha in/out at 6x/16x speed. Tracks combo changes to trigger digit transitions. Score display interpolates toward target. Sin wobble effect on score change. Scale pulses based on combo timer. Detects new highscore for display.

---

## 9. CoinCounter

**SUPERSEDED — see `docs/structs/hud.md` ## CoinCounter (2026-04-30 RE).**

Old summary preserved below for the size estimate / call-site context only. Ground-truth
struct size is **0xD4** (from `operator_new(0xd4)` in GameInit), not the 0x94 lower bound.
The text buffer at +0x94 is a 64-byte char array (filling 0x94..0xD3).

The class is **vestigial in the shipped binary** — Init is a no-op, no texture is ever
loaded into +0x74, the text buffer at +0x94 is never written, and `m_CoinCount` (+0x7C)
is never modified after the ctor zero. Draw renders nothing visible at runtime even
though the function is fully wired. See the new section in `hud.md` for full details.

---

## 10. VerticalScroller

**Constructors:** 0x00168230 (11 params), 0x00168304 (11 params)  
**Update:** 0x00167fd8 (90 lines)  
**Base class:** HUDControl3d  
**Estimated struct size:** ~0xA8 (highest field: 0xA4)

Touch-driven vertical scroller with up/down arrow buttons. Steps through a value range on swipe or button tap.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x78 | HUDControl3d | super | Base (pos/size from params) |
| 0x7C | long | m_MinValue | Minimum value |
| 0x80 | long | m_MaxValue | Maximum value |
| 0x84 | ushort | m_StepSize | Step per action |
| 0x88 | long | m_CurrentValue | Current value (clamped to min/max) |
| 0x8C | byte | m_NumArrows | Number of arrow buttons |
| 0x94 | ushort | m_VisibleRows | Visible row count (default 0x15 = 21) |
| 0x96 | ushort | m_TotalRows | Total row count |
| 0x98 | float | m_VisibleHeight | visibleRows * itemHeight |
| 0x9C | float | m_TotalHeight | totalRows * itemHeight |
| 0xA0 | byte | m_ScrollSpeed | Init 5 |
| 0xA1 | bool | m_WrapAround | Wrap at bounds |
| 0xA2 | byte | m_TouchAction | 0=none, 1=up, 2=down, 3=select |
| 0xA4 | int | m_TouchId | Touch ID (-1 = none) |

**Update behavior:** Calculates visible/total bounds from position and heights. On touch down, checks three touch regions: top arrow (scroll up = action 2 if wrap enabled, else 1), bottom arrow (scroll down), and middle (select = action 3 if within arrow count). On touch release, applies step: action 1 adds step to current value, action 2 subtracts step. Current value is clamped to [min, max]. Calls UpdateTouchPosition while touch held.

---

## See Also

- [Screens](../screens/) -- screen implementations using these widgets
- [HUD structs](hud.md) -- base classes for UI controls
