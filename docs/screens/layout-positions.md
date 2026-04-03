# Screen Element Positions (from binary analysis)

All positions are Vec3 floats in the original 480x320 coordinate system.
Origin appears to be center-screen; positive X = right, positive Y = up (or varies per screen).

---

## 1. PauseScreen

**Constructor**: `0x00155460`
**Update** (button creation): `0x00154468`

### Constructor Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_00155670` | 0.0 | Initial transition alpha |
| `DAT_00155674` | 320.0 | Screen height (used to compute title centering Y offset) |

Title position is computed as: `pos = (0.0, (320.0 - title_height) * 0.5, 0.0)`

### Single-Player Buttons

| Button | Field | Position (x, y, z) | DAT addresses (x, y, z) | Texture | Callback |
|--------|-------|---------------------|--------------------------|---------|----------|
| Resume | 0x98 | (240.0, -160.0, 0.0) | `DAT_001546e8`, `DAT_001546ec`, `DAT_001546f0` | `pause_button.tex` / `play_button.tex` (swapped) | `PauseScreen::ResumeGameCallback` via QCallee |
| Quit | 0xA0 | (0.0, 320.0, 0.0) | `DAT_001546f0`, `DAT_001546fc`, `DAT_001546f0` | `quit_title.tex` | `PauseScreen::QuitGameCallback` via QCallee |
| Retry | 0xAC | (0.0, 320.0, 0.0) | `DAT_00154968`, `DAT_0015496c`, `DAT_00154968` | `retry_button.tex` | `PauseScreen::RetryGameCallback` via QCallee |

**Resume button extra constants:**

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_001546f4` | 64.0 | Button hit-area padding or scale factor |
| `DAT_001546f8` | 500.0 | Button m_AnimSpeed (at MenuButton+0x154) |

**Notes:**
- Quit and Retry buttons are created at off-screen position (0, 320, 0) and then repositioned via `Nop_ShopScreen()` which copies a global Vec3 into MenuButton+0x124 (hit bounds).
- Retry button position is flipped via `operator*` with `local_48 = 0xBF800000` (-1.0f), negating its Y to place it on the opposite side.
- Resume button gets its hit bounds set from a scaled global Vec3 (via `DAT_00154710` pointer), then stored back to fields 0x8C-0x94.

### Same-Screen Multiplayer Buttons (created only if `IsSameScreenMultiplayer()`)

| Button | Field | Position (x, y, z) | DAT addresses (x, y, z) | Texture | Callback |
|--------|-------|---------------------|--------------------------|---------|----------|
| P2 Resume | 0x9C | (240.0, -160.0, 0.0) | `DAT_00154970`, `DAT_00154974`, `DAT_00154968` | `pause_button.tex` | QCallee\<PauseScreen\> |
| P2 Quit | 0xA4 | (0.0, 320.0, 0.0) | `DAT_00154c84`, `DAT_00154c6c`, `DAT_00154c84` | `quit_title.tex` | QCallee\<PauseScreen\> |
| P2 Retry | 0xB0 | (0.0, 320.0, 0.0) | `DAT_00154c84`, `DAT_00154c6c`, `DAT_00154c84` | `retry_button.tex` | QCallee\<PauseScreen\> |

**P2 Resume extra constants:**

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_00154c64` | 64.0 | P2 resume padding |
| `DAT_00154c68` | 500.0 | P2 resume anim speed |
| `DAT_00154c6c` | 320.0 | P2 quit/retry Y position |
| `DAT_00154c84` | 0.0 | P2 quit/retry X and Z |

### State Machine Thresholds

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_00154fb4` | 0.01 | Alpha fade-out threshold (state 0) |
| `DAT_00154fb8` | 0.0 | Alpha reset value |
| `DAT_00154fbc` | 0.999 | Alpha fade-in completion threshold (state 2->3) |
| `DAT_00154fc0` | 0.001 | Button press fadeout threshold (states 4-6) |

---

## 2. DojoScreen

**Constructor**: `0x00137b90`
**Update** (button creation): `0x00138414`

### Constructor Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_00137bf4` | (via reloc) | Initial m_TransitionAlpha |
| `DAT_00138684` | 0.95 | Alpha threshold for button creation |

### Buttons

| Button | Field | Position (x, y, z) | DAT addresses (x, y, z) | Texture | Callback |
|--------|-------|---------------------|--------------------------|---------|----------|
| Play | 0x94 | (185.0, -106.0, 0.0) | `DAT_00138688`, `DAT_0013868c`, `DAT_00138690` | From content SmartPtr (+0x17C) | QCallee\<DojoScreen\> (starts game) |
| Shop | 0x98 | (-18.0, -15.0, 0.0) | hardcoded: -18.0, -15.0 + `DAT_00138690` | From content SmartPtr (+0x14) | QCallee\<DojoScreen\> (opens ShopScreen) |
| About | 0x9C | (145.0, 42.0, 0.0) | `DAT_001389d0`, `DAT_001389d4`, `DAT_001389d8` | From content SmartPtr (+0x18) | QCallee\<DojoScreen\> (opens AboutScreen) |

### Extra Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_00138690` | 0.0 | Z coordinate (shared) |
| `DAT_00138694` | 0.825 | Play button hit-bounds scale (applied to +0x124 and +0x160 Vec3s) |
| `DAT_001386b4` | 0.575 | Shop button hit-area scale (applied to +0x140 Vec3) |
| `DAT_001389d8` | 0.0 | About button Z (same as 00138690) |
| `DAT_001389dc` | 0.999 | Alpha threshold for state 0->1 transition |
| `DAT_001389e0` | 0.001 | Fade-out completion threshold (states 2-4) |

**Notes:**
- Play button hit-bounds are scaled by 0.825 after creation.
- Shop button has custom hit-bounds from texture dimensions, then scaled by 0.575. Also sets `m_HighlightScale = 0.5` (0x3F000000) and `m_AnimScale` scaled by 0.575.
- Shop button checks `ItemManager::AreNewItems()` each frame to toggle "new" badge.
- Shop button position (-18, -15, 0) is hardcoded inline, not via DAT_ constants.

---

## 3. ShopScreen

**Constructor**: `0x0015cdac`
**Update** (button creation): `0x0015e1f4`

### Constructor Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_0015ce38` | (via reloc) | Initial alpha and buy delay |
| `DAT_0015e554` | 0.999 | Alpha threshold for transition completion |
| `DAT_0015e558` | 0.0 | Initial buy delay timer / Z coordinate for buttons |

### Buttons

| Button | Field | Position (x, y, z) | DAT addresses (x, y, z) | Texture | Callback |
|--------|-------|---------------------|--------------------------|---------|----------|
| Buy | 0x84 | (185.0, -105.0, 0.0) | `DAT_0015e55c`, `DAT_0015e560`, `DAT_0015e558` | From content SmartPtr (+0x17C) | QCallee\<ShopScreen\> + OnHighlight delegate |
| Equip | 0x8C | (145.0, 104.0, 0.0) | `DAT_0015e564`, `DAT_0015e568`, `DAT_0015e558` | From content SmartPtr (+0x14) | QCallee\<ShopScreen\> |

### Extra Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_0015e558` | 0.0 | Z for both buttons; also initial buy delay |
| `DAT_0015e55c` | 185.0 | Buy button X |
| `DAT_0015e560` | -105.0 | Buy button Y |
| `DAT_0015e564` | 145.0 | Equip button X |
| `DAT_0015e568` | 104.0 | Equip button Y |

**Notes:**
- Buy button is created in state 0 (transition-in completion). Has both OnPress (QCallee) and OnHighlight (Delegate1) callbacks.
- Equip button is created in state 1 only when item is not already equipped and not locked.
- Buy button has `m_bInteractive = true` (byte at +0x123 set).
- ShopListControl at field 0x94 manages item scrolling; scroll offset = `item_count + 0.5`.

---

## 4. AboutScreen

**Constructor**: `0x0012ecb8`
**Update** (button creation): `0x0012f020`

### Constructor Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_0012ed88` | 0.0 | Initial transition alpha |

### Buttons

| Button | Field | Position (x, y, z) | DAT addresses (x, y, z) | Texture | Callback |
|--------|-------|---------------------|--------------------------|---------|----------|
| Credits | 0x94 | (480.0, 0.0, 0.0) | `DAT_0012f2f8`, `DAT_0012f2f4`, `DAT_0012f2f4` | From `m_creditsTexture` static | Global delegate (not QCallee) |
| Back | 0x8C | (185.0, -106.0, 0.0) | `DAT_0012f300`, `DAT_0012f304`, `DAT_0012f2f4` | From content SmartPtr (+0x17C) | QCallee\<AboutScreen\> |

### Extra Constants

| Address | Value | Usage |
|---------|-------|-------|
| `DAT_0012f2f4` | 0.0 | Y and Z for Credits; Z for Back |
| `DAT_0012f2f8` | 480.0 | Credits button X (full screen width -- fills screen) |
| `DAT_0012f2fc` | 0.999 | Alpha threshold for Back button creation |
| `DAT_0012f300` | 185.0 | Back button X |
| `DAT_0012f304` | -106.0 | Back button Y |
| `DAT_0012f328` | 0.001 | Fade-out removal threshold (state 2) |

**Notes:**
- Credits button is created first (on initial Update call) as a full-width overlay at X=480.
- Credits button hit-bounds are set from texture dimensions (queried at runtime).
- Back button is created after alpha reaches 0.999, with `m_bInteractive = true`.
- Back button hit-bounds are scaled via `Vec3_ScaleConst` (same pattern as DojoScreen Play button).
- Static textures referenced: `AboutScreen::s_boardTexture`, `AboutScreen::m_creditsTexture`, `AboutScreen::m_senseiTexture`.

---

## Summary Position Table

All coordinates in 480x320 space, origin at top-left (with Y increasing downward in screen coords, but stored as signed floats relative to some pivot).

| Screen | Button | X | Y | Z | DAT_X | DAT_Y | DAT_Z |
|--------|--------|-----|------|-----|---------|---------|---------|
| Pause | Resume | 240.0 | -160.0 | 0.0 | `001546e8` | `001546ec` | `001546f0` |
| Pause | Quit | 0.0 | 320.0 | 0.0 | `001546f0` | `001546fc` | `001546f0` |
| Pause | Retry | 0.0 | 320.0 | 0.0 | `00154968` | `0015496c` | `00154968` |
| Pause | P2 Resume | 240.0 | -160.0 | 0.0 | `00154970` | `00154974` | `00154968` |
| Pause | P2 Quit | 0.0 | 320.0 | 0.0 | `00154c84` | `00154c6c` | `00154c84` |
| Pause | P2 Retry | 0.0 | 320.0 | 0.0 | `00154c84` | `00154c6c` | `00154c84` |
| Dojo | Play | 185.0 | -106.0 | 0.0 | `00138688` | `0013868c` | `00138690` |
| Dojo | Shop | -18.0 | -15.0 | 0.0 | (inline) | (inline) | `00138690` |
| Dojo | About | 145.0 | 42.0 | 0.0 | `001389d0` | `001389d4` | `001389d8` |
| Shop | Buy | 185.0 | -105.0 | 0.0 | `0015e55c` | `0015e560` | `0015e558` |
| Shop | Equip | 145.0 | 104.0 | 0.0 | `0015e564` | `0015e568` | `0015e558` |
| About | Credits | 480.0 | 0.0 | 0.0 | `0012f2f8` | `0012f2f4` | `0012f2f4` |
| About | Back | 185.0 | -106.0 | 0.0 | `0012f300` | `0012f304` | `0012f2f4` |
