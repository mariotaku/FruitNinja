<!-- Analysed: 2026-04-25T10:00 -->

# AboutScreen

**Binary refs:**

| Function | Address | Notes |
|----------|---------|-------|
| `AboutScreen::LoadContent` | `0x0012ec14` | static; loads 3 textures |
| `AboutScreen::AboutScreen(DojoScreen*)` | `0x0012ecb8` | instance ctor |
| `AboutScreen::~AboutScreen` | `0x0012eee0` | dtor |
| `AboutScreen::Update(float)` | `0x0012f020` | 226 insns |
| `AboutScreen::Draw(float*)` | `0x0012f394` | 403 insns |
| `Vec3_ScaleConst` | `0x0012e6bc` | multiplies Vec3 by 0.825 |

**Base class:** `HUDControl3d` (direct; no `BaseScreen`)

**Struct size:** ~0xA0 bytes

---

## Struct Layout

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| 0x00..0x7B | HUDControl3d | super | base fields (pos, size, layerFlags@0x34, etc.) |
| 0x74 | `SmartPtr<Texture>` | `field101_0x74` | per-instance copy of `s_TexHaiku` (set in ctor) |
| 0x7C | `float` | `m_TransitionAlpha` | lerped 0→1 in state 0, decays in state 2 |
| 0x8C | `MenuButton*` | `m_pBackButton` | lazily created when alpha > 0.9990 |
| 0x90 | `DojoScreen*` | `m_pParent` | parent for back-navigation (passed to ctor) |
| 0x94 | `MenuButton*` | `m_pOFNButton` | OpenFeint/GameCenter button (defunct, null in port) |
| 0x98 | `SmartPtr<Texture>` | `field_0x98` | OFN overlay tex (null in port, never assigned in ctor) |
| 0x9C | `int` | `m_State` | 0=transition-in, 1=idle, 2=transition-out |

Binary ctor initializes: `m_LayerFlags = 0x80`, `m_bNoDestructor = 0`, `m_State = 0`, `m_TransitionAlpha = 0.0` (DAT_0012ed88).

---

## Static Textures (LoadContent @ 0x0012ec14)

Three textures loaded once per process into GOT-relative static SmartPtrs:

| Variable | String address | File |
|----------|---------------|------|
| `s_TexHaiku` | 0x001BAE10 | `haikus.tex` |
| `s_TexCredits` | 0x001BAE1B | `credits.tex` |
| `s_TexSensei` | 0x001BB4D7 | `sensei.tex` |

A fourth load for `openfeint_gamecenter.tex` (0x001BAE27) is also present but goes to a separate slot used only by the OFN button (defunct in port).

Loading is guarded by a one-time flag at BSS offset `draw_base + DAT_0012ed94 + 0xc = 0x0022F1F0`.

---

## State Machine (Update @ 0x0012f020)

### OFN/GameCenter button check (state-independent, runs every frame)

```
if (s_TexSensei.IsValid() AND m_pOFNButton == nullptr):
    create MenuButton at (0, 480, 0) with AskUserToChoosePreferredNetwork callback
    store in m_pOFNButton (field121_0x94)
    texture from openfeint_gamecenter.tex
    sound callback: MenuCallbackClicked
```

The OFN button is ONLY created when `s_TexSensei` is loaded (the binary's loading
order means sensei is loaded 3rd; if LoadContent wasn't called this never fires).
Fruit type from `*(int**)(update_base + DAT_0012f324)` (bomb-threshold value = 0).

**Port**: stubs this block entirely. OFN/GameCenter is defunct.

### State 0 — Transition-in

```
alpha += (1.0 - alpha) * 0.125      // exponential approach
if alpha > 0.9990:                   // DAT_0012f2fc
    alpha = 1.0
    create back button
    m_State = 1
```

Back button creation:
- Texture: `game->field_0x17c` (back icon texture pointer loaded at runtime)
- Position: `(185, -106, 0)` (DAT_0012f300/f304)
- Fruit type: `*(int**)(update_base + DAT_0012f324)` (bomb-threshold)
- After Init: `Vec3_ScaleConst(button->m_TargetSize)` and `Vec3_ScaleConst(fruit->scale)` → multiply both by 0.825
- `strb 1, [button+0x138]` — sets byte at offset 0x138 (needs further RE)
- `TutorialControl::ResetTutePos(game->field_0x168, button)`
- Sound delete callback: `MenuCallbackClicked` (DAT_0012f314)

### State 1 — Idle

No-op.

### State 2 — Transition-out

```
alpha *= 0.75
if alpha < 0.001:                    // DAT_0012f328 = 0x3A83126F
    call parent->vtable[Reset]()     // vtable offset +0x10
    m_bPendingRemoval = 1            // field_0x33 = 1
```

`parent->vtable[Reset]()` calls DojoScreen's inherited Reset, which ultimately
resets DojoScreen state to 0 (fade-in), re-showing the Dojo buttons.

---

## Draw @ 0x0012f394  (4 render passes)

All passes use `m_TransitionAlpha` for slide-in curves.

### Block A — Background panel (haiku.tex via field101_0x74)

```
if not m_TexHaiku.IsValid(): skip
texH = m_TexHaiku.height
Y_start = 160.0 + texH * 0.5           // DAT_0012f690 + h*0.5  (cached in BSS)
Y_drawn = Y_start - (Y_start - 63.0) * alpha   // DAT_0012f694 = 63.0
X = -50.0                               // DAT_0012f698
Scale(texW+1, texH+1, 1), Translate(X, Y_drawn, 0), DrawQuad
```

The OFN button (field_0x94) also follows the panel:
```
if m_pOFNButton:
    button->pos = Vec3(BG_X + 132.0, Y_drawn + 70.0, 0)   // DAT_0012f6a0/a4
```

Two `Font::DrawString` calls use `game->field_0x54` (font pointer):
1. String at `draw_base + DAT_0012f6cc = 0x001BAE40` — a Utf8String in .rodata
   that happens to point at RTTI data in this Bada build (localization key absent).
   Drawn at `Y = Y_drawn + 97.0 - 10.0`, `X = -200.0`, RGB(0x74, 0x5D, 0x3C).
2. `GetVersionString()` — the game version number.
   Drawn at `X = -(strWidth * 14.0)` (measure once cached), same Y and colour.

**Port**: font draws stubbed (game->font slots not yet wired). Haiku text is embedded
in `haikus.tex` as a pre-rendered image, not in a font string.

### Block B — OFN overlay (field_0x98)

```
if m_TexOFNOverlay.IsValid():
    draw at (0.3 * texW - 50.0, Y_drawn + 0.3 * texH, 0)    // DAT_f6b4=0.3, f6b8=50
    Scale(ovW+1, ovH+1, 1)
```

**Port**: field_0x98 is always null, block is a no-op.

### Block C — Credits texture (s_TexCredits, slides up from below)

```
if not s_TexCredits.IsValid(): skip
Y_start = texH * -0.5 - 160.0           // cached in BSS
Y_drawn = Y_start - (Y_start + 96.0) * alpha   // DAT_0012f8dc = 96.0
X = -50.0                               // DAT_0012f8e0
Scale(texW+1, texH+1, 1), Translate(-50, Y_drawn, 0), DrawQuad_Colour
```

At alpha=0: Y = Y_start (below screen). At alpha=1: Y = Y_start - (Y_start + 96) = -96.

### Block D — Sensei texture (s_TexSensei, slides in from right)

```
if not s_TexSensei.IsValid(): skip
X_start = 240.0 + texW * 0.5           // DAT_0012f8e8 = 240.0 (cached in BSS)
X_drawn = X_start - (X_start - 155.0) * alpha   // DAT_0012f8ec = 155.0
Y = 56.0                               // DAT_0012f8f0
Scale(texW+1, texH+1, 1), Translate(X_drawn, 56, 0), Mesh::DrawQuadUnCached
```

Note: Block D uses `Mesh::DrawQuadUnCached` (not the standard `DrawQuad_Colour_Draw`
like blocks A-C). Port uses `Renderer::DrawQuad` for all blocks.

---

## Constants Summary

| Constant | Value | Source |
|---------|-------|--------|
| `ALPHA_LERP_IN` | 0.125 | from decompile |
| `ALPHA_IN_DONE` | 0.9990 | DAT_0012f2fc |
| `ALPHA_DECAY` | 0.75 | from decompile |
| `ALPHA_OUT_DONE` | 0.001 | DAT_0012f328 = 0x3A83126F |
| `POS_BACK_BUTTON` | (185, -106, 0) | DAT_0012f300/f304 |
| `BACK_SCALE` | 0.825 | DAT_0012e6e8 = 0x3F533333 |
| `POS_OFN_BUTTON` | (0, 480, 0) | DAT_0012f2f4/f2f8 |
| `BG_X` | -50 | DAT_0012f698 |
| `BG_Y_CACHE` | 160 | DAT_0012f690 |
| `BG_Y_REST` | 63 | DAT_0012f694 |
| `OFN_OFFSET_X` | 132 | DAT_0012f6a0 |
| `OFN_OFFSET_Y` | 70 | DAT_0012f6a4 |
| `FONT_TEXT_Y_OFFSET` | 97 | DAT_0012f6a8 |
| `FONT_X` | -200 | DAT_0012f6ac |
| `FONT_MAX_W` | 200 | DAT_0012f6b0 |
| `SENSEI_FRAC` | 0.3 | DAT_0012f6b4 |
| `SENSEI_X_OFS` | 50 | DAT_0012f6b8 |
| `CREDITS_X` | -50 | DAT_0012f8e0 |
| `CREDITS_Y_CACHE` | 160 | DAT_0012f8d8 |
| `CREDITS_Y_OFS` | 96 | DAT_0012f8dc |
| `SENSEI2_X_CACHE` | 240 | DAT_0012f8e8 |
| `SENSEI2_X_REST` | 155 | DAT_0012f8ec |
| `SENSEI2_Y` | 56 | DAT_0012f8f0 |

---

## See Also

- [DojoScreen](dojo.md) — parent screen; state 3 triggers AboutScreen push
- [HUD structs](../structs/hud.md) — HUDControl3d base class layout
- [Common screen patterns](common-patterns.md)
