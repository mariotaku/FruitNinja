# AboutScreen — Port-Ready RE Notes
<!-- Analysed: 2026-04-14T12:15 -->

Functions: ctor `0x0012ecb8`, Update `0x0012f020`, Draw `0x0012f394`,
LoadContent `0x0012ec14`, QuitGameCallback `0x0012eb30`.

---

## 1. Button positions

Two buttons created lazily in Update.

### Back/close button (`field119_0x8c`) — created when `m_State == 0` and alpha crosses threshold

Position: `(DAT_0012f300, DAT_0012f304, DAT_0012f2f4)` = `(185.0, -106.0, 0.0)` (0x0012f2f4).

Post-creation hit-rect scaled via `Vec3_ScaleConst` (same pattern as DojoScreen buttons).
`*(byte*)(button + 0x138) = 1` — sets some visibility/active flag at offset 0x138.

### Credits/info button (`field121_0x94`) — created first, when `m_State == 0` initially

Position: `(DAT_0012f2f8, DAT_0012f2f4, DAT_0012f2f4)` = `(480.0, 0.0, 0.0)` (off-screen right).
This is the **start** position before slide-in. The actual drawn position is computed each
frame in Draw based on `m_TransitionAlpha`.

Texture for credits button: `FruitType(name, false)` path (same as DojoScreen shop button —
a fruit-index type, `0xffffffff` passed as fruitType constant).

---

## 2. Button textures

Loaded by `AboutScreen::LoadContent` `0x0012ec14`, three textures into static slots
`s_boardTexture`, `m_creditsTexture`, `m_senseiTexture` (per symbol table):

| Static field | Filename | String address | Usage |
|---|---|---|---|
| `s_boardTexture` | `about.tex` | `0x001bb1ba` | background panel (`field101_0x74`); drawn in Draw section 1 |
| `m_creditsTexture` | `credits.tex` | `0x001bae1b` | credits panel texture; drawn in Draw section 2 (panel 2) |
| `m_senseiTexture` | `dojo_sensei.tex` | `0x001bb199` | sensei panel; drawn in Draw section 3 — skip per task |

Back button (`field119_0x8c`) uses a texture from the HUD's board texture `*(Texture**)(HUD + 0x17c)`,
not from AboutScreen's own LoadContent (same pattern as DojoScreen field1).

---

## 3. State machine transitions

`field106_0x7c` = `m_TransitionAlpha`, `field126_0x9c` = `m_State`.
Ctor sets `m_TransitionAlpha = DAT_0012ed88 = 0.0`, `m_State = 0`.

| State | Alpha update | Threshold / condition | Next state |
|-------|--------------|-----------------------|------------|
| 0 | `alpha += (1.0 - alpha) * 0.125` | `alpha > 0.9991` (`DAT_0012f2fc`) | lock alpha=1.0, create back button, → 1 |
| 0 | same | `alpha <= 0.9991` | stay; skip back-button creation |
| 1 | none | — | idle |
| 2 | `alpha *= 0.75` | `alpha < 0.001` (`DAT_0012f328`) | call parent Resume, set `m_bPendingRemoval=1` |

Lerp step: **0.125** (state 0 fade-in), decay factor: **0.75** (state 2 fade-out).

---

## 4. "Wait for entities cleared" mechanism

AboutScreen has **no such check**. This mechanism lives in DojoScreen state 4 only.
AboutScreen transitions directly on alpha threshold without checking actors.

---

## 5. Child screen creation site

AboutScreen creates **no child screens**. It is itself the child, created by DojoScreen
state 3 (see dojo-re-notes.md §5).

---

## 6. Release / pending-removal flow

`AboutScreen::QuitGameCallback` `0x0012eb30`:
1. Plays SFX via `GameSound::SFXPlay`.
2. Sets `this->field126_0x9c = 2` → starts state-2 fade-out.
3. Makes the FruitFact control visible and randomises its position.

State 2 fade (in `Update`):
```c
float a = this->field106_0x7c * 0.75f;
this->field106_0x7c = a;
if (a < 0.001f) {   // ARM: "if (-1 < (int)((uint)(a < 0.001f) << 31))"
    // Resume parent — calls DojoScreen vtable[4] (= BaseScreen::Resume or equivalent)
    (*(code**)((*(int*)this->field120_0x90) + 0x10))();
    this->field_0x33 = 1;   // m_bPendingRemoval — HUDControl offset 0x33
}
```

Parent resume is a **direct vtable call**: `(parent->vtable[4])()` — index 4 relative to
`HUDControl3d` vtable base (offset `+0x10` = vtable slot 4).
This is **not** a callback pointer stored in a field — it derives from `field120_0x90`
(the `DojoScreen*` stored at offset 0x90 in AboutScreen). After the vtable call completes,
AboutScreen marks itself `m_bPendingRemoval = 1` and HUD removes it next update.

---

## 7. HUDControl field offsets used

| Offset | Field | Value set |
|--------|-------|-----------|
| `0x33` | `m_bPendingRemoval` | `1` — triggers HUD removal |
| `0x34` | `m_LayerFlags` | `0x80` — written in ctor |
| `0x32` | `field_0x32` | `0` — cleared in ctor |
| `0x7c` | `m_TransitionAlpha` | float; also `field106_0x7c` in Ghidra |
| `0x8c` | `m_BackButton` | `MenuButton*`; also `field119_0x8c` |
| `0x90` | `m_ParentDojo` | `DojoScreen*`; `field120_0x90` |
| `0x94` | `m_CreditsButton` | `MenuButton*`; `field121_0x94` |
| `0x9c` | `m_State` | int; `field126_0x9c` |

---

## 8. Draw function details

`AboutScreen::Draw` `0x0012f394` — three independent draw passes, all conditional.

### Pass 1 — background board (`s_boardTexture` / `field101_0x74`)

Runs if `SmartPtr<field101_0x74>` is valid.

- `Texture::Set(field101_0x74)`
- Reset + Scale matrix from `(field29_0x20, field30_0x24, field31_0x28)` (stored dims from ctor)
- Y position computed with one-time-init local `start`:
  `start = DAT_0012f690 + height * 0.5` = `160.0 + h*0.5` (lazy-init via `__cxa_guard`)
  Drawn Y = `start - (start - DAT_0012f694) * m_TransitionAlpha`
  where `DAT_0012f694 = 63.0` (final resting Y).
- Translate to `(DAT_0012f698, drawn_Y, 0.0)` = `(-50.0, drawn_Y, 0.0)`.
- `DrawQuad_Colour_Draw` with board colour.
- `Texture::UnSet`
- Draws credits button if `field121_0x94 != 0`: offset from board pos by `(DAT_0012f6a0, DAT_0012f6a4, 0.0)` = `(132.0, 70.0, 0.0)`.

Text line above board:
- `Font::DrawString(DAT_0012f6ac_font, drawn_Y + DAT_0012f6a8 - 10.0, 0.0, 14.0 /*0x41600000*/, ...)`
  where `DAT_0012f6a8 = 97.0`, font size hardcoded `0x41600000 = 14.0`.
- First string: from static text blob `(GOT + DAT_0012f6cc)` in colour `RGB(0x74, 0x5d, 0x3c)`.
- Second string: `GetVersionString()` drawn at X offset `versX - DAT_0012f6b0`
  where `DAT_0012f6b0 = 50.0` and `versX` is lazily computed as `MeasureString * 14.0`.

Logo/swag sub-texture (`field_0x98`), if valid:
- Scale from `(w+1, h+1, 1.0)`.
- Position: `(DAT_0012f6b4 * field29_0x20 - DAT_0012f6b8, drawn_Y + DAT_0012f6b4 * field30_0x24, 0.0)`
  where `DAT_0012f6b4 = 0.3`, `DAT_0012f6b8 = 50.0`.

### Pass 2 — credits texture (`m_creditsTexture`, via `GOT + DAT_0012f8f4`)

Lazy-init local `creditsStart`:
`creditsStart = height * -0.5 - DAT_0012f8d8` = `h * -0.5 - 160.0` (negative, below screen).
Drawn Y = `creditsStart - (creditsStart + DAT_0012f8dc) * m_TransitionAlpha`
where `DAT_0012f8dc = 96.0`.
Translate to `(DAT_0012f8e0, drawn_Y, DAT_0012f8e4)` = `(-50.0, drawn_Y, 0.0)`.

### Pass 3 — sensei texture (`m_senseiTexture`, via `GOT + DAT_0012f900`)

**Skip per task.** Slides in from right:
Lazy-init `senseiStart = DAT_0012f8e8 + width * 0.5` = `240.0 + w*0.5`.
Drawn X = `senseiStart - (senseiStart - DAT_0012f8ec) * m_TransitionAlpha`, `DAT_0012f8ec = 155.0`.
Drawn Y = `DAT_0012f8f0 = 56.0`.
