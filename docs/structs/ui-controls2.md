# UI Controls - Batch 2

Decompiled from FruitNinja.exe (ARM32 Bada) via GhidraMCP.
All classes inherit from `HUDControl3d` (base size 0x7C).

---

## 1. BonusScreen

**Address:** Constructor `0x00132048`, Update `0x00132930`, Draw `0x0013325c`
**Base class:** HUDControl3d (0x7C)
**Estimated struct size:** ~0xC8 (highest field 0xC4 + 4)

Post-game bonus award display. Shows bonus awards sliding in sequentially, plays sounds, spawns particles, shakes camera, and awards coins.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00 | vtable* | vtable | |
| 0x00-0x7B | HUDControl3d | super | Base class (pos, size, layer, etc.) |
| 0x7C | void* | field1_0x7c | Set to 0 during Update (award state ptr?) |
| 0x80 | int | awardIndex | Current award index, incremented per award |
| 0x84 | vector\<BonusAwardHud\> | awards | Vector of individual award HUD entries |
| 0x90 | float | shakeAmplitude | Current shake amplitude, set in Shake() |
| 0x94 | float | shakeDecay | Shake decay copy, initialized = shakeAmplitude |
| 0x98 | float | shakeIntensity | Shake intensity, set in Shake() |
| 0x9C | short | shakeRandSeed | Random seed for shake offset |
| 0xA0 | Vector3 | screenPos | Position vector (3 floats, copied from global) |
| 0xAC | float | field24_0xac | Initialized to 1.0 |
| 0xB0 | int | field_0xb0 | Initialized to 0 |
| 0xB1 | byte | field_0xb1 | Initialized to 0 |
| 0xB2 | byte | field_0xb2 | Initialized to 0 |
| 0xB4 | int | loopSoundHandle | Background loop SFX handle, released when done |
| 0xB8 | float | timer | Main countdown timer (decremented by dt each frame) |
| 0xBC | Vector3 | cameraOffset | Camera shake/transition offset (3 floats) |

### State Machine (Update)

The `timer` field (0xB8) drives all state transitions:

1. **timer > timePerAward * (numAwards + 0.25):** Waiting phase, play loop sound when timer transitions into range
2. **0 < timer < fadeOutTime:** Fade-in phase with sine-wave camera animation, dimming background to 0.5
3. **timer < 0:** Slide-out phase with quadratic easing, camera returns to normal
4. **timer < -(slideTime + holdTime):** Pending removal (`m_bPendingRemoval = 1`)

AwardScores (`0x0013260c`) iterates over awards, spawns coins via `Coin::MakeCoins`, triggers particle emitters and sound effects per award.

---

## 2. ScreenFadeControl

**Address:** Constructor `0x0015aad0`, Update `0x0015a798`, Draw `0x0015a868`
**Base class:** HUDControl3d (0x7C)
**Estimated struct size:** ~0xBC (SmartPtr at 0xB4 = 8 bytes)

Full-screen fade overlay for transitions. Interpolates alpha over time, fires a delegate callback on completion.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x7B | HUDControl3d | super | Base; m_LayerFlags = 0x400 (top layer) |
| 0x7C | byte | isVisible | Whether the fade quad is drawn |
| 0x7D | byte | isActive | Whether fade is animating |
| 0x7E | byte | isFadeIn | 1 = fade-in (stays visible after), 0 = fade-out (becomes invisible) |
| 0x80 | float | elapsed | Current time elapsed in fade |
| 0x84 | float | duration | Total duration of fade |
| 0x88 | Colour | color | Fade overlay color (4 bytes RGBA) |
| 0x8B | byte | currentAlpha | Current interpolated alpha |
| 0x8C | byte | initialAlpha | Starting alpha |
| 0x8D | byte | startAlpha | Source alpha for lerp |
| 0x8E | byte | endAlpha | Target alpha for lerp |
| 0x90 | Delegate0\<void\> | onComplete | Callback delegate fired on fade completion (size ~0x24) |
| 0xB4 | SmartPtr\<Texture\> | texture | Fade texture (white quad) |

### State Machine (Update)

- If `isActive`:
  - `elapsed += dt`
  - If `elapsed <= duration`: lerp alpha from `startAlpha` to `endAlpha` by `(elapsed / duration)`
  - If `elapsed > duration`: call `OnFadeComplete()` which sets final alpha, clears `isActive`, and fires the `onComplete` delegate

### Key Methods

- **StartFade** (`0x0015a7f0`): Sets up fade direction (`startAlpha`/`endAlpha` = 0xFF/0x00 or vice versa), resets elapsed, activates fade
- **CancelFade** (`0x0015a764`): Clears `isVisible` and `isActive`
- **OnFadeComplete** (`0x0015a770`): Sets alpha to `endAlpha`, optionally clears visibility if fade-out, fires delegate

---

## 3. ScreenTint

**Address:** Constructor `0x0011e678`, Parse `0x0011d2d0`
**Struct size:** 0x28 (plain struct, no vtable -- stored in std::vector)

Power-up tint overlay definition. A lightweight value type loaded from XML, stored in vectors. Describes a color tint transition applied to the screen during power-ups.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00 | float | field_0x00 | Init to 0.0 |
| 0x04 | float | transitionSpeed | Parsed from XML "speed" attribute |
| 0x08 | float | startTime | Parsed from XML "start" |
| 0x0C | float | endTime | Parsed from XML "end" |
| 0x10 | float[3] | colorStart | RGB start color (parsed from "colour_start" attribute) |
| 0x1C | float[3] | colorEnd | RGB end color (initially copied from colorStart, then parsed from "colour_end") |

### Constructor

All floats initialized to 0.0 except offset 0x08 which is set to 1.0 (`0x3f800000`).

### Parse (from XML)

Reads attributes: `start` (0x08), `end` (0x0C), `colour_start` -> colorStart (0x10, 3 floats), copies to colorEnd (0x1C), then parses `colour` -> overwrites colorStart (0x10) and `colour_end` -> overwrites colorEnd (0x1C). Also reads `speed` -> transitionSpeed (0x04).

---

## 4. ComboControl

**Address:** Constructor `0x00136d1c`, Update `0x00136be4`
**Base class:** HUDControl3d (0x7C)
**Estimated struct size:** ~0x8C (field at 0x84 is char[8])

Combo counter pop-up that briefly displays the current combo count, then self-destructs.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x7B | HUDControl3d | super | m_bNoDestructor set to 0 |
| 0x7C | float | lifetime | Countdown timer, starts at 1.0, decremented by dt |
| 0x80 | int | comboCount | Combo value passed to constructor |
| 0x84 | char[8] | label | Formatted string (e.g. "x3") via SPrintf |

### State Machine (Update)

Extremely simple:
- `lifetime -= dt`
- If `lifetime < 0`: set `m_bPendingRemoval = 1` (self-destruct)

Total visible duration: 1.0 seconds.

---

## 5. NotificationControl

**Address:** Constructor `0x00153060`, Update `0x00152a00`, Draw `0x001531f8`
**Base class:** HUDControl3d (0x7C)
**Estimated struct size:** ~0x110 (highest field 0x10C + 1)

In-game notification pop-up (e.g. "New Best!", achievement unlocks). Supports different notification types with distinct animations and sounds.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x7B | HUDControl3d | super | m_LayerFlags = 8 |
| 0x7C | float | scale | Text scale, starts at 16.0, clamped to fit screen |
| 0x80 | float | elapsed | Time accumulator |
| 0x84 | int | value | Numeric value (e.g. score) |
| 0x88 | char[0x80] | text | Formatted notification text (uppercase) |
| 0x108 | char[4] | valueStr | Formatted value string (e.g. "x5") or empty |
| 0x10C | byte | notificationType | 0=normal, 1=with value, 2=special (particles + SFX) |

### Notification Types (enum)

- **0x00**: Standard notification (no value display)
- **0x01**: Notification with numeric value
- **0x02**: Special notification with particle effects and bonus SFX

### State Machine (Update)

Driven by `elapsed` (0x80), with three timing phases:

1. **Phase 1 (0 to riseTime ~0.3s):** Slide-in from off-screen with quadratic easing. Position interpolated from start to target based on `(elapsed/riseTime)^2`
2. **Phase 2 (riseTime to holdEnd ~2.5s):** Hold at final position. For type 2: spawns particle emitters every 1/8 second with random X offsets
3. **Phase 3 (holdEnd to maxTime ~3.0s):** Slide-out with reverse quadratic easing. If `elapsed > maxTime`: set `m_bPendingRemoval = 1`

Type 2 notifications get slightly different Y positions (higher on screen).

---

## 6. ProgressionTimerControl

**Address:** Constructor `0x00157dbc`, Update `0x00157bb0`, Draw `0x001579f4`
**Base class:** HUDControl3d (0x7C)
**Estimated struct size:** ~0x110 (delegate at 0xCC + ~0x24 delegate size + padding)

Game-over countdown timer display. Shows a ticking timer that counts down to zero, with fade-in/out animation.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x7B | HUDControl3d | super | Position set to (-0.5, 0.5, 0) anchor, scaled 18.0 Y |
| 0x7C | float | field_0x7c | Init 0.0 |
| 0x80 | float | timeRemaining | Countdown seconds remaining |
| 0x84 | float | fadeAlpha | Fade opacity (0..1), drives visibility |
| 0x88 | byte | field_0x88 | Init 0 |
| 0x8C | char[0x40] | timerText | Formatted countdown string (e.g. "3") via SPrintf + ceil |
| 0xC8 | byte | isActive | Whether countdown is running |
| 0xC9 | byte | isPaused | Whether countdown is paused |
| 0xCA | byte | isVisible | Controls fade direction |
| 0xCB | byte | field_0xcb | Init 0 |
| 0xCC | Delegate | onTimeExpired | Callback delegate fired when timer hits 0 |

### State Machine (Update)

1. **Fade logic**: If `isVisible` (0xCA), fade alpha increases by `3.0 * dt`; otherwise decreases by `3.0 * dt`. Clamped to [0.0, 1.0].
2. **Countdown**: If `isActive` (0xC8) and not `isPaused` (0xC9):
   - `timeRemaining -= dt`
   - If `timeRemaining <= 0`: reset to 0, call `OnTimeExpired()` delegate
   - Format `ceil(timeRemaining)` into `timerText`

### Draw

Only draws when `fadeAlpha > 0`. Applies quadratic fade-out offset (`(1-fadeAlpha)^2 * scale`) to Y position. Uses tinted colour from base class.

---

## 7. GenericHUDControl

**Address:** Constructor `0x00143a28` (13-param), Update `0x00143598`, PreDraw `0x00143640`
**Base class:** HUDControl3d (0x7C)
**Estimated struct size:** ~0x1C4 (highest field 0x1C0 + 4)

General-purpose HUD element with transition animations, pulsing effects, and texture display. Used as a building block for various HUD widgets (score popups, icons, etc.). Managed in a `std::list<GenericHUDControl*>`.

### Fields

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| 0x00-0x7B | HUDControl3d | super | |
| 0x7C | float | field_0x7c | Init 0.0 |
| 0x64 | Vector2 | uvMin | Texture UV rect min (from _Vector2 param) |
| 0x6C | Vector2 | uvMax | Texture UV rect max |
| 0x74 | SmartPtr\<Texture\> | texture | Display texture |
| 0x80 | TransitionInfo | transIn_pos | Position transition (size 0x18) |
| 0x98 | TransitionInfo | transIn_scale | Scale transition |
| 0xA8 | float | field_0xa8 | Init 1.0 |
| 0xB0 | TransitionInfo | transIn_alpha | Alpha/opacity transition |
| 0xC4 | float | field_0xc4 | Init 0.0 |
| 0xC8 | TransitionInfo | transOut | Fourth transition |
| 0xE0 | PulseInfo | pulse_pos | Position pulse (size 0x28) |
| 0x108 | PulseInfo | pulse_scale | Scale pulse |
| 0x130 | PulseInfo | pulse_alpha | Alpha pulse |
| 0x158 | PulseInfo | pulse_extra | Fourth pulse |
| 0x180 | Vector3 | targetPos | Target/final position |
| 0x18C | Vector3 | savedPos1 | Stored position copy 1 |
| 0x198 | Vector3 | savedPos2 | Stored position copy 2 |
| 0x1A4 | Vector3 | sizeVec | Size vector (computed from texture dimensions * UV rect) |
| 0x1B0 | Vector3 | offsetPos | Offset position copy |
| 0x1BC | float | field_0x1bc | Init 0.0 |
| 0x1C0 | float | startParam1 | First constructor float param |
| 0x1C4 | float | startParam2 | Second constructor float param |

### Sub-structs

- **TransitionInfo** (size 0x18): Constructed via `TranisitionInfo::TranisitionInfo()`, evaluated via `GetAmt(float progress)` -- returns interpolated value for position/scale/alpha transitions.
- **PulseInfo** (size 0x28): Constructed via `PulseInfo::PulseInfo()`, evaluated via `GetPulseAmt(float time)` -- returns oscillating pulse offset.

### Update

Minimal: just increments the elapsed time field:
```
this->elapsed += dt;   // field at offset ~0x7C
```

### PreDraw

Complex animation compositor:
1. Computes normalized progress: `(elapsed - startTime) / (endTime - startTime)`, clamped to [0,1]
2. Evaluates all 4 TransitionInfo structs at current progress to get position offset, scale multiplier, alpha/opacity, and a fourth channel
3. Evaluates all 4 PulseInfo structs at elapsed time for oscillating overlays
4. Combines transition + pulse results:
   - Position = base + transition_pos_offset * pulse_pos
   - Scale = base * transition_scale * pulse_scale
   - Alpha = base_alpha + transition_alpha + pulse_alpha
   - Opacity byte (0x5F) = clamp(trans_alpha * pulse_alpha * 255, 0, 255)

---

## Summary Table

| Class | Base | Size | Key Purpose |
|-------|------|------|-------------|
| BonusScreen | HUDControl3d | ~0xC8 | Post-game bonus award sequence |
| ScreenFadeControl | HUDControl3d | ~0xBC | Full-screen fade transitions |
| ScreenTint | (none) | 0x28 | Color tint definition (value type) |
| ComboControl | HUDControl3d | ~0x8C | Combo counter popup (1s lifetime) |
| NotificationControl | HUDControl3d | ~0x110 | In-game notification with 3 types |
| ProgressionTimerControl | HUDControl3d | ~0x110 | Game-over countdown timer |
| GenericHUDControl | HUDControl3d | ~0x1C8 | Animated HUD element with transitions + pulses |

---

## See Also

- [Screens](../screens/) -- screen implementations using these controls
- [HUD structs](hud.md) -- base classes for UI controls
