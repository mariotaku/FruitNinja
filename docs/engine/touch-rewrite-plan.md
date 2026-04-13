# Touch-System Rewrite Plan

<!-- Drafted: 2026-04-13T19:00 -->

Binary-accurate rewrite of the port's touch input layer. Replaces the current
`SDLInputTranslator → InputManager::DispatchEvent → MainScreen::HandleTouchDown
→ MenuButton::TouchDown` callback chain with the binary's poll-based architecture
centred on `Mortar::Touch`.

## Context

### What the port has now

| Component | File | Role |
|---|---|---|
| `SDLInputTranslator` | `src/platform/SDLInputTranslator.cpp` | Translates SDL mouse/touch events → `InputEvent` with StringHash'd action names, dispatches to `InputManager` |
| `InputManager` | `src/engine/input/InputManager.h`, `src/platform/InputManager.h` (duplicated) | Action-hash → callback dispatch. Port wires `TouchDown_0`/`TouchMove_X0`/`TouchUp_0` hashes in `GameInit` |
| `MainScreen::HandleTouchDown/Up` | `src/screens/MainScreen.cpp` | Port-specific shim; iterates buttons, calls `MenuButton::TouchDown` on hit |
| `MenuButton::TouchDown/Up/HitTest` | `src/hud/MenuButton.cpp` | Callback entry points that fire `m_ClickCallback` on release inside the button rect |
| `Mortar::Touch` | `src/engine/input/Touch.{h,cpp}` | **Orphaned.** Double-buffered 8-slot state with a ring-buffered event queue; nothing ever calls `PushEvent`, `Update`, or reads the front buffer |
| `SlashEntity::TouchDown/TouchUp` | `src/entities/SlashEntity.cpp` | Receives touches via `InputManager` lambdas registered in `GameInit` |

### What the binary does

- `Mortar::Touch` is the canonical per-frame touch-state singleton. Its layout (at `+0xa0`) is a **flat 16-slot array**, 12 bytes per slot: `{float x, float y, float phase}`. No front/back buffer; no touch-id; no ring queue.
- `phase` values: `0.0 = inactive`, `1.0 = just pressed`, `2.0 = held`. On release the phase transitions back to `0.0` (via a release-edge path in `Mortar::Touch::Update` — TODO verify).
- Bada's `Osp::Ui::ITouchEventListener` calls into `Mortar::Touch` from the `GlesForm::OnTouch*` handlers to update the slot array.
- **Polling consumers** read the slot state each frame via three free functions:
  - `TouchInRegion(float left, float right, float top, float bottom, int preferredSlot)` at `0x001691cc` — returns slot index inside the rect (preferring `preferredSlot` if valid), or `-1`.
  - `IsTouchDown(int slot)` at `0x00169144` — returns `1` if `phase==1.0`, `2` if `phase==2.0`, else `0`.
  - `UpdateTouchPosition(this)` / `MenuButton::UpdateTouchPosition` at `0x0014e3c4` — copies `(x, y, phase)` from `Touch+slot*12+0xa0..+0xa8` into the caller's `+0xdc/+0xe0/+0xe4` trio (the last-known touch position for that button).
- `MenuButton::Update` at `0x0014e614` polls via those helpers. Its touch block:
  1. If no slot tracked (`field_0xd8 == -1`): call `TouchInRegion(...)` over the button rect. If hit: latch the slot id in `field_0xd8`. If `IsTouchDown==2` (held from a previous frame), invoke `m_ClickCallback` on the pressing edge.
  2. If a slot is tracked: call `IsTouchDown(field_0xd8)`. If `0` (released): if touch-up position is inside the button rect, call `TouchReleased()` → fire `m_ClickCallback`. Clear `field_0xd8 = -1`.
  3. Each frame while tracked: call `UpdateTouchPosition()` to refresh `field_0xdc..+0xe4` for animations/highlight logic.
- `InputManager` also exists in the binary for **keyboard/gamepad/action** bindings (loaded from `game_input.txt`). Touch is **not** routed through `InputManager` — the hash names like `TouchDown_0` in the binary's config file are a port-author invention. In the binary, action hashes bind keyboard keys and the gamepad D-pad, not touches.

### Implications

1. The port has **two parallel touch systems** glued by a shim. The `InputManager` path works but is not binary. The `Mortar::Touch` path is implemented but orphaned.
2. `MainScreen::HandleTouchDown/Up` and `MenuButton::TouchDown/Up/HitTest` do not exist in the binary. They are port-specific.
3. `SlashEntity::TouchDown/TouchUp` receive events via InputManager lambdas in the port, but the binary polls `Mortar::Touch` directly from `SlashEntity::Update`.
4. The port's `Mortar::Touch` struct (28-byte slots, front/back buffers, touch-id counter) does not match the binary's layout (12-byte slots, single state array, no id).

## Goals

1. Port `Mortar::Touch` to the binary's exact layout (16 × 12-byte slots at `+0xa0`, `phase` semantics).
2. Port the three free functions `TouchInRegion`, `IsTouchDown`, `UpdateTouchPosition`.
3. Rewrite `MenuButton::Update`'s touch block to match the binary's polling flow. Delete `TouchDown`, `TouchUp`, `HitTest` (replaced by `TouchInRegion`).
4. Delete `MainScreen::HandleTouchDown` / `HandleTouchUp` (not in binary).
5. Remove the `TouchDown_0`/`TouchMove_X0`/`TouchUp_0` bindings from `GameInit`. Keep `InputManager` itself for future keyboard/gamepad use, but unused for touch.
6. Rewire `SlashEntity::Update` to poll `Mortar::Touch::GetTouch(0)` instead of the callback path. Keep `UpdateTouchDown` / `AddPoint` / `TouchUp` logic internal.
7. Wire SDL events directly into `Mortar::Touch::PushEvent` (or equivalent), not through `InputManager`. Drain and apply in `Mortar::Touch::Update` once per frame.
8. Delete the duplicate `src/platform/InputManager.h` and `src/platform/InputEvent.h` files if not used elsewhere. (Separate cleanup — out of scope for this rewrite.)

## RE prerequisites (do these first)

Any field offset listed below without an ARM address is a placeholder that must be verified from Ghidra before coding.

1. **`Mortar::Touch` full struct.** Decompile its constructor and `Update` method to confirm:
   - Size (suspected ~192 + header = ~0x160)
   - Fields before `+0xa0` (header/metadata)
   - Whether there's a ring buffer for pending events, or if `Mortar::Touch::Update` just transitions phases each frame
   - How `phase` transitions from `1.0 → 2.0 → 0.0` (just-pressed → held → released → inactive)
2. **Bada `GlesForm::OnTouch*` handlers.** Find the `Osp::Ui::ITouchEventListener` vtable entries and see how they push into `Mortar::Touch`. Identify the writer entry point.
3. **`MenuButton::Update`** at `0x0014e614` — already decompiled. Re-read the touch block carefully and extract:
   - Rect computation: `(pos ± size/2 ± anim offsets)`
   - Which `field_0x??` is `m_TouchSlot` (suspected `+0xd8`) and which is `m_TouchPos` (suspected `+0xdc/+0xe0/+0xe4`)
   - Exact release-callback condition
4. **`SlashEntity::UpdateTouchDown`** at `0x0017D2E4` — already RE'd in the SlashEntity report. Confirm it reads from `Mortar::Touch`, not from an InputManager callback state.
5. **`InputManager::RegisterInputCallback`** at `0x0019683C` — confirm that in the binary, the hashes registered at startup are keyboard actions, not `TouchDown_N`.

## Port changes, per file

### `src/engine/input/Touch.{h,cpp}` — rewrite

Replace the 28-byte `TouchState` + 28 × 8 front/back buffer with a binary-matching layout:

```cpp
namespace Mortar {

class Touch {
public:
    static const int MAX_SLOTS = 16;
    static Touch& GetInstance();

    // Binary-matching slot array (12 bytes per slot, starting at this+0xa0
    // in the original; port drops the placement constraint but keeps the
    // flat layout).
    struct Slot {
        float x;
        float y;
        float phase;   // 0=inactive, 1=just-pressed, 2=held
    };
    Slot m_Slots[MAX_SLOTS];

    // Per-frame: transition just-pressed → held, drain pending release edges.
    void Update();

    // Called from SDL translator (mouse/finger event).
    void OnPressed (int slot, float x, float y);
    void OnMoved   (int slot, float x, float y);
    void OnReleased(int slot);
};

} // namespace Mortar
```

Delete the old `TouchState` / `TEvnt` / ring-buffer machinery. `PushEvent` becomes an internal helper used by `OnPressed/Moved/Released`.

Add a header-level declaration for the free functions next to the class:

```cpp
int  TouchInRegion(float left, float right, float top, float bottom, int preferredSlot);
int  IsTouchDown  (int slot);   // 0, 1, or 2 matching binary
```

### `src/platform/SDLInputTranslator.{h,cpp}` — rewire

- Remove `InputManager::DispatchEvent(ie)` calls for touch actions (keep the StringHash action infrastructure in place for later keyboard binding).
- Replace with direct `Mortar::Touch::GetInstance().OnPressed/Moved/Released(channel, gx, gy)` calls.
- `MapFingerId / ReleaseFingerId` remain — they map SDL's 64-bit `SDL_FingerID` to stable 0..15 slot indices.
- Mouse emulation keeps using slot `0`.
- The `TransformTouch*` helpers already output binary-centred coords — no change.

### `src/game/GameInit.cpp` — strip InputManager touch wiring

Remove the lambdas registered for `TouchDown_0`, `TouchMove_X0`, `TouchUp_0`. Remove the `InputManager::ClearActions()` call in `GameExit_Handler` if nothing else uses it. Add one per-frame `Mortar::Touch::GetInstance().Update()` call at the top of `GameUpdate`, before the HUD/entity updates (so polling consumers see fresh state).

### `src/hud/MenuButton.{h,cpp}` — rewrite touch block

Delete `TouchDown`, `TouchUp`, `HitTest` methods. Add private helpers:

```cpp
class MenuButton {
private:
    int   m_TouchSlot;  // +0xd8 — -1 if not tracking a touch
    float m_TouchX;     // +0xdc
    float m_TouchY;     // +0xe0
    float m_TouchPhase; // +0xe4
    // ... existing fields
};
```

Move the touch polling into `MenuButton::Update`:

```cpp
void MenuButton::Update(float dt) {
    // ... existing animation + rotation code ...

    // Compute button rect (including anim bounce offsets from the binary)
    float l = pos.x - size.x * 0.5f - m_AnimSpeed2;
    float r = pos.x + size.x * 0.5f + m_AnimSpeed2;
    float t = pos.y + size.y * 0.5f + m_AnimSpeed;
    float b = pos.y - size.y * 0.5f - m_AnimSpeed;

    if (m_TouchSlot == -1) {
        int slot = TouchInRegion(l, r, b, t, -1);
        if (slot >= 0) {
            m_TouchSlot = slot;
            if (IsTouchDown(slot) == 2) {
                // Press edge — fire callback per binary Update path
                if (m_bVisible && m_FruitType < 0) m_ClickCallback();
            }
        }
    } else {
        int state = IsTouchDown(m_TouchSlot);
        if (state == 0) {
            // Release — check if release position was inside the rect
            m_TouchSlot = -1;
            if (m_TouchX >= l && m_TouchX <= r &&
                m_TouchY >= b && m_TouchY <= t) {
                if (m_ClickCallback) m_ClickCallback();
            }
            // Restore default size/anim on release
        } else {
            UpdateTouchPosition();   // refresh m_TouchX/Y/Phase from slot
            // ... existing highlight logic ...
        }
    }
}

void MenuButton::UpdateTouchPosition() {
    if (m_TouchSlot < 0) return;
    const Mortar::Touch::Slot& s = Mortar::Touch::GetInstance().m_Slots[m_TouchSlot];
    m_TouchX     = s.x;
    m_TouchY     = s.y;
    m_TouchPhase = s.phase;
}
```

The actual binary flow is more nuanced (animations on press, shake timer on release, highlight state) — follow the decompile at `0x0014e614` literally.

### `src/screens/MainScreen.{h,cpp}` — delete shim

Remove:
- `MainScreen::HandleTouchDown`
- `MainScreen::HandleTouchUp`
- Any `public:` label bump I added in MainScreen.h for those.

### `src/entities/SlashEntity.{h,cpp}` — poll-based trail ingestion

Delete `SlashEntity::TouchDown(float, float)` and `SlashEntity::TouchUp()`. Move the logic into `SlashEntity::Update(dt)` and poll `Mortar::Touch::GetTouch(0)`:

```cpp
void SlashEntity::Update(float dt) {
    const Mortar::Touch::Slot& s = Mortar::Touch::GetInstance().m_Slots[0];
    if (s.phase == 1.0f || s.phase == 2.0f) {
        // Touch held — ingest point at (s.x, s.y)
        // (same logic the current TouchDown method has)
        IngestTouchPoint(Vec3(s.x, s.y, 0.0f));
    } else if (m_bHasHead) {
        // Touch released — begin fade
        m_State = 2;
        m_bHasHead = false;
    }
    // ... existing RebuildGeometry + physics decay ...
}
```

The binary's `SlashEntity::UpdateTouchDown` at `0x0017D2E4` uses slot 0 for player 1 (single-player path). Multi-player would poll slot 1 too; skipped here.

## Implementation order

1. Rewrite `Mortar::Touch` (struct + methods). No consumers yet — builds clean.
2. Add `TouchInRegion` / `IsTouchDown` free functions in the same header.
3. Wire `SDLInputTranslator` to feed `Mortar::Touch::OnPressed/Moved/Released`.
4. Call `Touch::Update()` once per frame from `GameUpdate`.
5. Rewrite `MenuButton::Update` to poll and delete the `TouchDown/Up/HitTest` trio. Update `MenuButton.h` field list.
6. Remove `MainScreen::HandleTouchDown/Up` + the `public:` label.
7. Remove the `TouchDown_0` / `TouchMove_X0` / `TouchUp_0` `InputManager` bindings from `GameInit`. Keep `InputManager` itself (dormant).
8. Rewrite `SlashEntity::Update` to poll `Mortar::Touch::GetTouch(0)`. Delete `TouchDown`/`TouchUp` methods.
9. Build + smoke test each step.

Each step is one focused commit so any regression bisects cleanly.

## Test plan

At each step:

1. **Build** — `cmake --build build` must finish clean.
2. **Smoke run** — `./build/fruit-ninja.exe` for ≤5s, no crash, no GL errors, blade texture loads.
3. **Menu interaction** (after step 5): click inside Play button → logs show the `GameModeCallback`. Click outside → no callback. Click-and-drag from empty area → blade trail draws, Play button does not fire.
4. **Slash trail** (after step 8): drag anywhere → trail draws along the cursor path. Release → trail fades.
5. **Diff test** — `git diff` each commit should be ~1 file, small and focused.

## Rollback strategy

Each step is its own commit. If a step breaks something non-obvious, `git revert <sha>` and restart from the last working state. The current `HEAD` (`eba02af`, menu + blade demo working) is the fallback — always be able to return to it.

## Out of scope

- Collision (slash × fruit). Deferred until WaveManager + full gameplay state are ported.
- Multi-touch fruit slicing (SlashEntity player index 2). The binary has a player-index axis that the port is ignoring until local multiplayer comes back.
- Deleting the duplicate `src/platform/InputManager.h` / `InputEvent.h` files. Already dormant; remove in a separate cleanup pass.
- `game_input.txt` parser. The binary loads keyboard/gamepad bindings from this file at startup; the port never has. Deferred.
- `TouchReleased()` global state (binary calls it from `MenuButton::Update` on release-in-rect). The port will inline the equivalent into the button's own release branch.

## Risks

- **`Mortar::Touch::Update` phase transitions**: need to understand the exact edge rules before porting. Decompile it first or risk UI ghost-clicks on first frame.
- **`m_TouchSlot` vs InputManager double-click**: until step 7 lands, both paths fire. Test after step 5 that the old InputManager callbacks are idempotent (which they are — the binding list is unchanged).
- **Field offsets**: `+0xd8`/`+0xdc`/`+0xe0`/`+0xe4` for `m_TouchSlot / m_TouchX / Y / Phase` are my current guess. Verify in Ghidra before editing `MenuButton.h`.

## See also

- [`camera.md`](camera.md) — `FruitCamera` and `MortarCamera` overview
- [`coordinate-system.md`](coordinate-system.md) — binary ortho and coord-space reference
- [`touch-system.md`](touch-system.md) — existing (partial) Touch doc, will need an update after the rewrite
- [`touch-input.md`](touch-input.md) — input pipeline overview
- Port: `src/entities/SlashEntity.cpp` — blade trail visual
- Binary: `TouchInRegion` 0x001691cc, `IsTouchDown` 0x00169144, `MenuButton::Update` 0x0014e614, `SlashEntity::UpdateTouchDown` 0x0017D2E4
