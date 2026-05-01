# TutorialControl & MainScreen::OnMenuItemsCleared — Deep RE

Analysed: 2026-05-02. Binary: `FruitNinja.exe` (Bada ARM32, Mortar Engine).

## TL;DR

- `TutorialControl` is **not** a first-launch tutorial. It is the contextual
  "swipe arrow" hint that animates over a `MenuButton` (typically the Play
  button) **when slow-motion is active on the main menu** or during a
  screen transition that has reached `|m_TransitionTimer| > 0.99`.
- There is **no save-data persistence flag** for "tutorial seen". The hint
  re-fires whenever its trigger conditions hold.
- `MainScreen::OnMenuItemsCleared` (`0x0014ac98`) is a literal `bx lr`
  no-op in this build. It is invoked by `MenuButton::Update` after
  `ClearMenuItems()` has launched all menu buttons off-screen, but **only
  when `g_GameData->pMainScreen != null`**. The hook exists in the source
  tree but does nothing in shipped binary. The actual menu state-machine
  drives off `m_State` and `ActorManager::GetNumEntities()==0` — see the
  case-3 / case-0x17 branches in `MainScreen::Update`.
- Existing port `src/hud/TutorialControl.{h,cpp}` is already faithful. The
  only meaningful gaps are (a) `CanShowTute()` returning `false`
  unconditionally because `HUD::m_globalTimeScale` is not yet ported, and
  (b) the binary's `pGameOverScreen != null` guard not being honoured.

---

## Section 1 — Struct layout

`TutorialControl : HUDControl3d` — total size **0xA0 (160 B)**, allocated
in `GameInit` via `operator new(0xa0)` at `g_GameData + 0x168`.

Fields specific to `TutorialControl` (parent `HUDControl3d` ends at +0x7C):

| Offset | Size | Type                       | Field                  | Purpose |
|--------|------|----------------------------|------------------------|---------|
| +0x7C  | 4    | float                      | `m_AnimTimer`          | -10.0 = inactive sentinel; counts up by `dt` while `CanShowTute()`; show range `[0, 2.75)`. |
| +0x80  | 12   | Vec3                       | `m_DrawPos`            | Computed each `Update`; final translate applied in `Draw`. |
| +0x8C  | 4    | SmartPtr\<Mortar::Texture\> | `m_PressTex`           | `press_indicate.tex`. Trail quads in `Draw`. |
| +0x90  | 4    | Colour                     | `m_Colour`             | Arrow draw colour; alpha driven by anim phase. |
| +0x94  | 4    | int (used as 0/1 bool)     | `m_UVFrame`            | UV frame selector for arrow (0 → u=[0.0,0.5], 1 → u=[0.5,1.0]). NOT a visibility gate. The port currently calls this `m_bHidden`. |
| +0x98  | 4    | float                      | `m_HalfWidth`          | Stored half-extent of associated MenuButton (`btn->m_TargetSize.x − btn->m_AnimSpeed2*2 − 10`, halved if > 256). |
| +0x9C  | 1    | bool                       | `m_bFlipX`             | Arrow points left (`pos.x > 0`) XOR (`btn->m_bScoreSubmitted`). |
| +0x9D..+0x9F | 3 | (padding)               |                        |         |

Inherited from `HUDControl3d / HUDControl` (via `super.m_Texture` at +0x74):
the **arrow** texture (`swipe_fruit_begin.tex`, GLuint id) lives in
the parent's `m_Texture` field — verified at ctor `0x001636f8`.

### Vtable (`_ZTV15TutorialControl` @ `0x001ea1a8`, slot 0 = typeinfo offset)

Vtable bytes after the 8-byte typeinfo header (decoded little-endian, Thumb LSB stripped):

| vtable +N | Slot # | Symbol               | Address       | Notes |
|-----------|--------|----------------------|---------------|-------|
| +0x00     | 0      | dtor1 (D1)           | `0x0016363c`  | |
| +0x04     | 1      | dtor0 (D0, deleting) | `0x00163698`  | |
| +0x08     | 2      | `Init`               | `0x00162e38`  | Sets `m_LayerFlags=8`, calls vtable[Reset]. |
| +0x0C     | 3      | `Release`            | `0x00162e48`  | Empty. |
| +0x10     | 4      | `Reset`              | `0x00162e4c`  | `m_AnimTimer = -10.0f`. |
| +0x14     | 5      | (HUD base)           | `0x0012f92c`  | Inherited stub. |
| +0x18     | 6      | `PreDraw(float*)`    | `0x00162fb0`  | `return param;` — pass-through. |
| +0x1C     | 7      | `Draw(float*)`       | `0x00163360`  | Draws trail then arrow. |
| +0x20     | 8      | (HUD base)           | `0x0012f930`  | `PreDrawOrder` thunk. |
| +0x24     | 9      | (HUD base)           | `0x0012f93c`  | |
| +0x28     | 10     | `Update(float)`      | `0x00163014`  | |
| +0x2C     | 11     | `SetToMultiplayerState` | `0x00162fb4` | `return 0;` — empty. |
| +0x30..   | 12..14 | (HUD base helpers)   | `0x0012f948`+ | |

The "callback that the engine fires" hypothesised in the task brief
(slot 0x14) is `SetToMultiplayerState`, and it is empty.

---

## Section 2 — Method bodies (pseudocode)

### Constructor `TutorialControl::TutorialControl()` @ `0x001636f8`

```cpp
TutorialControl::TutorialControl() {
    HUDControl3d::HUDControl3d(&this->super);
    this->super.vtable = &_ZTV15TutorialControl + 8;
    SmartPtr<Texture>::SmartPtr(&this->m_PressTex);
    Colour::Colour(&this->m_Colour);                     // default-init

    SmartPtr<Texture> t1; TextureManager::LoadLocalisedTexture(t1, "swipe_fruit_begin.tex");
    this->super.m_Texture = t1;                          // ARROW

    SmartPtr<Texture> t2; TextureManager::LoadLocalisedTexture(t2, "press_indicate.tex");
    this->m_PressTex = t2;                               // TRAIL

    this->super.m_LayerFlags = 8;
}
```

(`m_AnimTimer`, `m_DrawPos`, `m_UVFrame`, `m_HalfWidth`, `m_bFlipX` are
NOT zero-initialised by the ctor; they are set by the first `Reset` call
that `Init` triggers.)

### `Init` @ `0x00162e38`

```cpp
void TutorialControl::Init() {
    this->super.m_LayerFlags = 8;
    this->vtable[Reset]();        // virtual call; resolves to TutorialControl::Reset
}
```

### `Reset` @ `0x00162e4c`

```cpp
void TutorialControl::Reset() {
    this->m_AnimTimer = -10.0f;
}
```

### `Release` @ `0x00162e48`

```cpp
void TutorialControl::Release() { /* empty */ }
```

### `CanShowTute` @ `0x00162fb8` — disassembly-verified

```cpp
bool TutorialControl::CanShowTute() {
    Game* g = g_GameData;                                    // GOT-relative load
    if (fabsf(g->m_TransitionTimer /* +0xC */) > 0.99f)      // s15 = 0x3F7D70A4
        return true;
    if (g->pGameOverScreen /* +0x164 */ == nullptr) return false;
    HUD* hud = g->pHud /* +0x3c */;
    if (hud == nullptr) return false;
    return hud->m_globalTimeScale /* +0x20 */ < 1.0f;        // slow-motion gate
}
```

Important: the existing port stub `if (!game->hud) return false;` is
correct, but the binary returns **false** when `pGameOverScreen` is
**null**, not true. Stay with `return false` until the timeScale field
exists.

### `Update(float dt)` @ `0x00163014`

Already faithfully ported. Confirmed against decompile + DAT loads:

| DAT     | Value   | Meaning |
|---------|---------|---------|
| `0x00163270` | `-1000.0f` | "off-screen" sentinel for `m_DrawPos`. |
| `0x00163274` | `-0.075f` | Lerp Y start. |
| `0x00163278` | `0.0f`    |          |
| `0x0016327c` | `0.15f`   | Lerp Y end. |
| `0x00163280` | `0.35f`   | `PHASE_FADEIN`. |
| `0x00163284` | `255.0f`  | Alpha max. |
| `0x00163288` | `0.6f`    | `PHASE_BOUNCE`. |
| `0x0016328c` | `-254.0f` | Fade-out slope (255 − 254·(2*(t−2.25)) → 1 at t=2.75). |

Update pseudocode (matches existing port; `field_0x7c = m_AnimTimer`):

```cpp
void TutorialControl::Update(float dt) {
    m_LayerFlags = 8;
    m_DrawPos = Vec3(-1000, -1000, -1000);
    m_Colour = (zeroed local);   // local copy, not stored back
    m_UVFrame = 1;               // default: arrow shows UV frame 1

    if (!CanShowTute()) {
        m_AnimTimer = -10.0f;
        m_DrawPos += pos;
        return;
    }

    if (m_AnimTimer >= 2.75f) {           // ARM idiom on `< 2.75`
        m_DrawPos += pos;
        return;
    }

    m_AnimTimer += dt;                    // <-- always advances when CanShowTute

    // Lerp scale (-0.5..1.0 in X, -0.075..0.15 in Y) from t = (timer-1)*2 ∈ [0,1]
    float t = clamp((m_AnimTimer - 1.0f) * 2.0f, 0.0f, 1.0f);
    Vec3 scale = Vec3(-0.5, -0.075, 0) + Vec3(1.0, 0.15, 0) * t;
    m_DrawPos = scale * m_HalfWidth;
    if (m_bFlipX) m_DrawPos.x = -m_DrawPos.x;

    if (m_AnimTimer <= 0.0f) { m_DrawPos += pos; return; }

    // Active phases
    if (m_AnimTimer < 0.35f) {                     // FADE-IN
        float a = (m_AnimTimer / 0.35f) * 255.0f;
        m_DrawPos.y += 20.0f;
        m_UVFrame = 0;
        m_Colour.a = clamp((uint8_t)a, 0, 255);
    } else if (m_AnimTimer < 0.6f) {               // BOUNCE-DOWN
        float f = m_AnimTimer - 0.35f;
        m_DrawPos.y += f * 4.0f * -20.0f + 20.0f;
        m_UVFrame = 0;
        // alpha unchanged
    } else if (m_AnimTimer < 1.0f                  // HOLD (binary has 3 stacked
            || m_AnimTimer < 1.5f                  // early-exits, all goto LAB)
            || m_AnimTimer < 2.25f) {
        // ↓ falls through to LAB_0016325a immediately:
        // do NOT touch m_UVFrame or m_Colour.a; UV stays at 1, alpha sticky.
        // Net visible behaviour: arrow held with prior alpha, on UV frame 1.
    } else if (m_AnimTimer < 2.75f) {              // FADE-OUT
        float f = 255.0f + (m_AnimTimer - 2.25f + (m_AnimTimer - 2.25f)) * (-254.0f);
        m_DrawPos.y += 20.0f;
        m_UVFrame = 0;
        m_Colour.a = clamp((uint8_t)f, 0, 255);
    } else {                                        // OUT (>= 2.75)
        m_DrawPos.y += 20.0f;
        m_UVFrame = 0;
        m_AnimTimer = -10.0f;
    }

    m_DrawPos += pos;       // LAB_0016325a
}
```

### `Draw(float* hudScale)` @ `0x00163360`

```cpp
void TutorialControl::Draw(float* hudScale) {
    float flipSign = m_bFlipX ? -1.0f : 1.0f;
    if (m_AnimTimer <= 0.0f) return;

    // (1) TRAIL — only during HOLD window: 0.6 < timer < 2.25
    if (0.6f < m_AnimTimer && m_AnimTimer < 2.25f) {
        for (int i = 0; i < 4; ++i) {
            int rem  = (int)(m_AnimTimer * 2000.0f) % 1000;
            float frac = (float)i + (float)rem / 1000.0f;
            float baseA = clamp(255.0f + (frac - 3.0f) * -255.0f, 0, 255);
            float a;
            if      (m_AnimTimer < 0.85f) a = baseA * (m_AnimTimer - 0.6f) * 4.0f;
            else if (m_AnimTimer >  2.0f) a = baseA + (m_AnimTimer - 2.0f) * -4.0f * baseA;
            else                          a = baseA;

            float quadScale = (2.0f * frac) * (2.0f * frac);
            m_PressTex->Set();
            ResetMatrix();
            ScaleMatrix(g_OnesVec3 * quadScale);   // global Vec3(1,1,1)
            TranslateMatrix(m_DrawPos);
            UploadMatrices();
            DrawQuadSized(0.0f, 1.0f, Colour(255,255,255, (uint8_t)clamp(a,0,255)));
            m_PressTex->UnSet();
        }
    }

    // (2) ARROW
    super.m_Texture->Set();
    ResetMatrix();
    ScaleMatrix(Vec3(flipSign * 96.0f, 96.0f, 1.0f));
    Vec3 offsetUnit = Vec3(flipSign * -0.125f, -0.40625f, 0.0f);
    Vec3 drawAt = m_DrawPos - (offsetUnit * 96.0f);
    TranslateMatrix(drawAt);
    UploadMatrices();
    float u0 = (float)m_UVFrame * 0.5f;
    float u1 = u0 + 0.5f;
    DrawQuadSized(u0, u1, m_Colour);
    super.m_Texture->UnSet();
}
```

DAT references in Draw:

| DAT     | Value | Use |
|---------|-------|-----|
| `0x001635ac` | `0.6f`     | Trail-window low (`PHASE_BOUNCE`). |
| `0x001635b0` | `2000.0f`  | Trail timer scale. |
| `0x001635b4` | `255.0f`   | Alpha max. |
| `0x001635b8` | `1000.0f`  | Trail mod divisor. |
| `0x001635bc` | `-255.0f`  | Trail alpha slope. |
| `0x001635c0` | `0.85f`    | Trail fade-in end. |
| `0x001635c4` | `0.0f`     | UV V0 / Z constant. |
| `0x001635c8` | `96.0f`    | Arrow scale. |

### `ResetTutePos(MenuButton*)` @ `0x00162f04`

```cpp
void TutorialControl::ResetTutePos(MenuButton* btn) {
    if (btn) {
        pos = btn->pos;
        float halfWidth = btn->m_TargetSize.x  /* +0x124 */
                        - btn->m_AnimSpeed2     /* +0x14c */ * 2.0f
                        - 10.0f;
        if (halfWidth > 256.0f) halfWidth *= 0.5f;
        m_HalfWidth = halfWidth;

        m_bFlipX = (pos.x > 0.0f);
        if (btn->m_bScoreSubmitted /* +0x120 */) m_bFlipX = (pos.x <= 0.0f);
    }
    m_AnimTimer = -10.0f;
}
```

### `ResetTutePos(const Vec3&)` @ `0x00162f84`

```cpp
void TutorialControl::ResetTutePos(const Vec3& p) {
    pos = p;
    m_bFlipX = (pos.x > 0.0f);
    m_AnimTimer = -10.0f;
}
```

### `ButtonPressedAtPos(MenuButton*)` @ `0x00162e58`

Identical to `ResetTutePos(MenuButton*)` body, plus:

```cpp
if (m_AnimTimer < 0.0f) {       // guard: only when inactive
    /* ... same body as ResetTutePos(btn), but does NOT reset m_AnimTimer ... */
    m_AnimTimer += 9.5f;        // -10 → -0.5  (animation starts in 0.5 s)
    if (m_AnimTimer > 0.0f) m_AnimTimer = 0.0f;
}
```

### `PreDraw(float*)` @ `0x00162fb0`

```cpp
float* PreDraw(float* p) { return p; }   // pass-through
```

### `SetToMultiplayerState()` @ `0x00162fb4`

```cpp
int  SetToMultiplayerState() { return 0; }  // empty stub
```

---

## Section 3 — `MainScreen::OnMenuItemsCleared`

### Real symbol @ `0x0014ac98`

```text
0014ac98:  bx lr
```

```cpp
void MainScreen::OnMenuItemsCleared() { /* empty */ }
```

**One byte function. It is a stub.** The hook is real (it has a vtable
slot and a stub thunk at `0x001034d0` going through `PTR_OnMenuItemsCleared_001f1640`),
but the implementation in the shipped binary does nothing. Halfbrick
likely planned to use it for chained transitions but never filled it.

### Caller — `MenuButton::Update` @ `0x0014e614`

The thunk @ `0x001034d0` (which dispatches to `0x0014ac98`) is invoked
in only **one** place — the fruit-piece "menu button got slashed" branch
of `MenuButton::Update`:

```cpp
// MenuButton::Update, after a slash hit causes the fruit-piece to fly off:
if (this->m_bEnabled) {
    ClearMenuItems();                          // launch all other menu fruits
    if (g_GameData->pMainScreen != nullptr) {
        MainScreen::OnMenuItemsCleared();      // <-- this hook
    }
}
```

So the runtime contract is "fired once, the moment the user slices a
menu fruit-piece, *while* `MainScreen` is active". In the shipped build
it's a no-op. Tutorial state is **not** managed here.

### Real menu state-machine driver

Tutorial-relevant calls live in `MainScreen::Update` (`0x0014b278`):

| State | Call                                             | Purpose |
|-------|--------------------------------------------------|---------|
| 1 (init Play button) | `TutorialControl::ResetTutePos(g_GameData->pTutorialCtrl, this->pPlayButton)` | Snap position to Play button, clear timer to -10. |
| 0x17 (`QUIT_WAIT`)   | `TutorialControl::ResetTutePos(g_GameData->pTutorialCtrl, NULL)`            | Hide. |
| 0x18 (`QUIT_BOMB`)   | `TutorialControl::ResetTutePos(g_GameData->pTutorialCtrl, NULL)`            | Hide. |

`ButtonPressedAtPos` is never called in the shipped binary (only the
thunk at `0x0010502c` exists; nothing references it). The animation is
driven solely by `Update` advancing `m_AnimTimer` whenever `CanShowTute()`
returns true.

---

## Section 4 — Persistence (or lack thereof)

There is **no save-data field** for "tutorial seen". Confirmation:

- `search_strings("SeenTutorial|FirstPlay|HasSeen")` → no matches related
  to tutorial.
- `search_strings("tutorial|tute")` → only function/symbol names, no
  `_baked_strings.dat` keys, no XML attribute names, no preference IDs.
- `FruitSaveData` xrefs (`0x000fe010`, `0x00103c08`, `0x00129cb4`,
  `0x00129e74`, `0x0016e2fc`) — none of them touch a tutorial flag.
- `docs/systems/save-system.md` — no tutorial entry.
- The static symbol `_ZZN10MainScreen20UpdateScreenElementsEffE4tute`
  resolves to `0x001f3d64`, an `int*` initialised to `0x3F800000` (1.0).
  It is read/written only by the **logo bounce** code path inside
  `MainScreen::UpdateScreenElements` (xrefs `0x0014adde [W]`,
  `0x0014ae32 [W]`, `0x0014ae42 [R]`). It is a misnamed local-static
  used to scale the logo bounce alpha — **not** related to the
  tutorial control.

**Conclusion**: the tutorial re-fires every time the trigger conditions
become true. It does not record progress. The port should not introduce
a "seen" save flag.

---

## Section 5 — Action list for `implementer`

Existing port at `src/hud/TutorialControl.{h,cpp}` is already
substantially correct. The remaining work is small and bounded.

### Tier 1 — make the visible-arrow path actually fire

**T1.1**: In `HUD`, port `m_globalTimeScale` (offset `+0x20` of HUD base
class). It is the slow-motion multiplier used by the engine when slicing
the last fruit of a wave. Value range `[<1.0, 1.0]` where `1.0` = normal
speed and `<1.0` = slow-mo. Without this, `CanShowTute()` returns false
forever and the arrow never plays.

**T1.2**: In `TutorialControl::CanShowTute()`, after T1.1 lands, replace
the current placeholder `return false` with the binary-faithful logic:

```cpp
bool TutorialControl::CanShowTute() const {
    Game* g = Game::GetInstance();
    if (!g) return false;
    if (fabsf(g->m_TransitionTimer) > 0.99f) return true;
    if (!g->pGameOverScreen) return false;
    if (!g->hud) return false;
    return g->hud->m_globalTimeScale < 1.0f;
}
```

The `pGameOverScreen` guard is correct — until GameOverScreen is
allocated (i.e. after the player dies once), the slow-mo branch cannot
trigger. This matches binary semantics; do not relax it.

**T1.3**: Verify `MainScreen` calls `TutorialControl::ResetTutePos(playBtn)`
during state 1 init. The port should mirror the binary site at
`0x0014b6f8` (case 1 of `MainScreen::Update`'s switch), inside the
"`pPlayButton == nullptr`" creation block, immediately after wiring up
the `Delegate1`. (Existing `docs/screens/main.md` lines 542-545 already
prescribe this — re-check the port.)

### Tier 2 — state-machine completeness (optional fidelity)

**T2.1**: Port `MainScreen::OnMenuItemsCleared` as an empty function.
Wire `MenuButton::Update` to call it (after `ClearMenuItems()`) only when
`g_GameData->pMainScreen != nullptr`. This is dead code in the shipped
binary, but porting it preserves the symbol/architecture.

**T2.2**: In `MainScreen::Update` cases 0x17 and 0x18 (`QUIT_WAIT`,
`QUIT_BOMB`), call `TutorialControl::ResetTutePos(NULL)` to hide the
arrow on quit. (See `docs/screens/main.md:400` and `:425`.)

**T2.3**: `ButtonPressedAtPos`, `SetToMultiplayerState`, `PreDraw`,
`Release` — port as the empty/identity bodies given in Section 2. None
of them are referenced in the shipped game flow but they belong on the
vtable for completeness.

**T2.4 (skip)**: There is **no** save-data persistence work to do.
Explicitly do not introduce a "tutorial seen" flag.

### Things the port is already getting right

- Texture wiring: `swipe_fruit_begin.tex` → `super.m_Texture` (arrow);
  `press_indicate.tex` → `m_PressTex` (trail).
- Animation timing constants 0.35 / 0.60 / 2.25 / 2.75 / 9.5 / 20.0 / 96.0.
- `m_UVFrame` (currently named `m_bHidden` — please rename) is a
  UV-half-selector, not a visibility gate. Existing code already treats
  it correctly in `Draw`.
- `m_HalfWidth` halve-not-clamp at 256.0.

### Naming nit

`m_bHidden` (port name for +0x94) is misleading — Update sets it to 1
each frame and individual phases clear it to 0. It is a UV-frame
selector. Suggested rename: `m_UVFrame` or `m_uvHalf`. The Draw code is
the only consumer and treats it as `* 0.5` for u-coordinate. (No
behaviour change — just clarity.)

---

## Section 6 — References

### Function addresses

| Function                                 | Address     |
|------------------------------------------|-------------|
| `TutorialControl::TutorialControl`       | `0x001636f8` |
| `TutorialControl::~TutorialControl` (D2) | `0x001635dc` (within object code; near vtable D1) |
| `TutorialControl::Init`                  | `0x00162e38` |
| `TutorialControl::Release`               | `0x00162e48` |
| `TutorialControl::Reset`                 | `0x00162e4c` |
| `TutorialControl::ButtonPressedAtPos`    | `0x00162e58` |
| `TutorialControl::ResetTutePos(MenuButton*)` | `0x00162f04` |
| `TutorialControl::ResetTutePos(Vec3)`    | `0x00162f84` |
| `TutorialControl::SetToMultiplayerState` | `0x00162fb4` |
| `TutorialControl::CanShowTute`           | `0x00162fb8` |
| `TutorialControl::PreDraw`               | `0x00162fb0` |
| `TutorialControl::Update`                | `0x00163014` |
| `TutorialControl::Draw`                  | `0x00163360` |
| `MainScreen::OnMenuItemsCleared`         | `0x0014ac98` |
| `MainScreen::OnMenuItemsCleared` (thunk) | `0x001034d0` |
| `MainScreen::Update`                     | `0x0014b278` |
| `MenuButton::Update` (caller)            | `0x0014e614` |
| `ClearMenuItems`                         | `0x0016ac7c` |

### Vtables / GOT / globals

| Symbol                                             | Address     |
|----------------------------------------------------|-------------|
| `_ZTV15TutorialControl` (vtable)                   | `0x001ea1a8` |
| `_ZZN10MainScreen20UpdateScreenElementsEffE4tute`  | `0x001f3d64` (logo-bounce float, NOT tutorial) |
| `PTR_OnMenuItemsCleared_001f1640`                  | `0x001f1640` |
| `PTR_ButtonPressedAtPos_001f1f5c`                  | `0x001f1f5c` |
| `PTR_ResetTutePos_001ed40c`                        | `0x001ed40c` |
| `Game::pTutorialCtrl` (offset within `g_GameData`) | `+0x168`    |
| `Game::m_TransitionTimer`                          | `+0x0c`     |
| `Game::pHud`                                       | `+0x3c`     |
| `Game::pMainScreen`                                | `+0x160`    |
| `Game::pGameOverScreen`                            | `+0x164`    |

### Asset names

| Texture path (passed through `LoadLocalisedTexture`) | Field           | Role |
|------------------------------------------------------|-----------------|------|
| `swipe_fruit_begin.tex` (`0x001bc2cc`)               | `super.m_Texture` (+0x74) | The 2-frame swipe arrow sprite (UV halves). |
| `press_indicate.tex`                                 | `m_PressTex` (+0x8C)      | Trail/echo quads. |

(No prompt-text / font resource — the tutorial is purely visual, no
strings.)

### MenuButton fields used

| Offset | Field                     | Used by `ResetTutePos` / `ButtonPressedAtPos` |
|--------|---------------------------|------------------------------------------------|
| +0x08  | `pos` (Vec3)              | copied verbatim into `TutorialControl::pos`. |
| +0x120 | `m_bScoreSubmitted` (bool) | XORs the `flipX` calculation. |
| +0x124 | `m_TargetSize.x` (float)  | used to derive `halfWidth`. |
| +0x14C | `m_AnimSpeed2` (float)    | used to derive `halfWidth` (`-2*v`). |
