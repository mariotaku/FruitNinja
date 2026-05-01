# PauseScreen — Deep RE

Analysed: 2026-05-02. Binary: `FruitNinja.exe` (Bada ARM32, Halfbrick Mortar).

Replaces / consolidates the older surface notes in `docs/screens/pause.md`.
Existing port stub: `src/screens/PauseScreen.{h,cpp}` (size 0xd8 confirmed).

## 1. Location in binary

| Symbol | Address | Notes |
|---|---|---|
| `PauseScreen::PauseScreen()` | `0x00155460` | Real ctor body (101 lines). PLT thunk at `0x00101778`. Second copy `0x00155228` (likely COMDAT dup). |
| `PauseScreen::~PauseScreen()` (deleting) | `0x001540b8` | Calls `Release`, then `operator_delete`. |
| `PauseScreen::~PauseScreen()` (in-place) | `0x00154150` | Same body without `operator_delete`. |
| `PauseScreen::Init()` (vtable[2]) | `0x00153e28` | 5-instr thunk: `(*vtable[4])(this)` — i.e. forwards to Reset. No bespoke LoadContent work. |
| `PauseScreen::BeginDraw()` (vtable[5]) | `0x00153e44` | Sets `super.m_LayerFlags = 8`. |
| `PauseScreen::PreDraw()` (vtable[6]) | `0x0016bda0` | Draws full-screen black-tinted `flash.tex` quad as the dim overlay (only when `m_LayerFlags == 8`). |
| `PauseScreen::DrawOrder()` (vtable[9]) | `0x00153e98` | Calls `HUDControl3d::Draw(this, layerArg)` only when `field+0x7c (alpha) > 0.0` and not online MP. This is what renders `pause_title.tex`. |
| `PauseScreen::Update(float)` (vtable[10]) | `0x00154468` | 569 lines. State machine + lazy button creation + position/alpha lerping. |
| `PauseScreen::SetToMultiplayerState()` (vtable[11]) | `0x00154060` | Late-binding swap: replaces Resume button texture with `m_RetryButtonTex` (used during MP transitions). |
| `PauseScreen::PauseGameCallback()` | `0x001542d0` | Delegate target of the **Resume / in-game-Pause** button. State 0->2 (pause) or 3->4 (resume). |
| `PauseScreen::PauseGameCallback2()` | `0x00154400` | Delegate target of the **Retry** button. Wraps PauseGameCallback, also marks `field+0xcc=1`. |
| `PauseScreen::QuitGameCallback()` | `0x00153ebc` | Delegate target of the **Quit** button. State 3 -> 6, clears save totals/combo, sets `field+0xc8 (LastHitButton) = 0`. |
| `PauseScreen::IsEnabled()` | `0x00153e4c` | Predicate used by Update — gates whether button alpha (`field+0xb4`) decays towards 1 or 0. Reads MainScreen state at game+0x1c (offset 0xc / 0x10 / 0x05). |
| Vtable header (PauseScreen) | `0x001e9bf8` | Resolved via GOT[`0x000750c`]. |
| typeinfo string `"11PauseScreen"` | `0x001bbe14` | Itanium ABI mangled name. |

Caller of the ctor: **GameInit step 12** at `0x0016cae0` (in `GameInit @ 0x0016c644`). Allocated with `operator_new(0xd8)`, stored at `g_TaskState + 0x04`, then `vtable[2] (Init)` is called immediately, then `HUD::AddControl(g_Game.hud, pauseScreen, false)` at step 14. No "MainScreen pushes PauseScreen" — both are independent HUD children added in the same init pass; activation/visibility is purely state-driven inside `Update`.

## 2. Struct layout (0xd8 bytes)

Confirmed against ctor + Update writes. Inherits `HUDControl3d` (size 124 = 0x7c), so PauseScreen-specific fields begin at `0x7c`. (Older `docs/screens/pause.md` had `m_PauseTitleTex` at 0x74 separately — that's actually the inherited `HUDControl3d::m_SecondaryTex`; the title texture is stored there, not in a 0x7c slot.)

```
struct PauseScreen : HUDControl3d {            // 0x00..0x77 base
    /* 0x74 */ SmartPtr<Texture>  m_SecondaryTex;   // INHERITED — pause_title.tex
    /* 0x7c */ float              m_Alpha;          // primary fade [0..1]; ctor sets 0.0
    /* 0x80 */ Vec3               m_TitleSize;      // size copy used for DrawOrder
    /* 0x8c */ Vec3               m_ButtonOriginPos;// re-evaluated each Update from Resume button pos
    /* 0x98 */ MenuButton*        m_ResumeButton;   // P1 — uses pause_button.tex / play_button.tex
    /* 0x9c */ MenuButton*        m_P2ResumeButton; // P2 (same-screen MP only)
    /* 0xa0 */ MenuButton*        m_QuitButton;     // P1 — uses quit_title.tex
    /* 0xa4 */ MenuButton*        m_P2QuitButton;   // P2 (MP only)
    /* 0xa8 */ SmartPtr<Texture>  m_PauseButtonTex; // pause_button.tex (in-game pause icon)
    /* 0xac */ MenuButton*        m_RetryButton;    // P1 — uses retry_button.tex
    /* 0xb0 */ MenuButton*        m_P2RetryButton;  // P2 (MP only)
    /* 0xb4 */ float              m_ButtonFadeAlpha;// secondary fade for buttons; ctor sets 1.0
    /* 0xb8 */ SmartPtr<Texture>  m_PlayButtonTex;  // play_button.tex (resume icon when paused)
    /* 0xbc */ SmartPtr<Texture>  m_QuitTitleTex;   // quit_title.tex
    /* 0xc0 */ SmartPtr<Texture>  m_RetryButtonTex; // retry_button.tex
    /* 0xc4 */ undefined4         _pad_c4;          // not written in ctor; cleared by HUDControl3d base
    /* 0xc8 */ int                m_LastHitButton;  // ctor=0xFFFFFFFF; QuitGameCallback sets 0
    /* 0xcc */ int                m_PressIndex;     // 0 default; PauseGameCallback2 (Retry) sets 1
    /* 0xd0 */ float              m_RevealTimer;    // ctor sets 0.0; counts down, gates state 0
    /* 0xd4 */ int                m_State;          // state machine [0..6]; ctor sets 0
};                                                  // total 0xd8
```

Notes:
- `m_LayerFlags` (inherited at base+0x34) is set to **8** in ctor and re-asserted by `BeginDraw`. PreDraw uses `m_LayerFlags == 8` as gate.
- `pos` (inherited at base+0x08) is set in ctor to `(0, (320 - sizeY) * 0.5, 0)` — i.e. centered along Y by texture height. Update overrides `pos.y` each frame to `240.0 + sizeY + (-2.0 * m_Alpha)` (slide-in animation).
- The on-screen button position scratch `m_ButtonOriginPos` (offset 0x8c..0x94) is overwritten every Update from the Resume button's `m_ScreenPos` (Resume offset +0x125). It's a per-frame layout cache.

## 3. Vtable

Vtable starts at `0x001e9bf8`. The ctor stores `vtable_addr + 8` (skipping top_offset and typeinfo per Itanium ABI), so call indices below are relative to `vtable+8`. Slot order matches `HUDControlFns` (15 slots inherited from HUDControl/HUDControl3d).

| Idx | Slot name | Function | Behavior |
|----|---|---|---|
| 0 | dtor1 (deleting) | `0x00154150` | ~PauseScreen + delete |
| 1 | dtor2 (in-place) | `0x001540b8` | ~PauseScreen, no delete |
| 2 | Init / LoadContent | `0x00153e28` | Forwards to vtable[4] (Reset) |
| 3 | Release | `0x0014408c` | Inherited HUDControl::Release (not its own function) |
| 4 | Reset | `0x00144024` | Inherited HUDControl base |
| 5 | BeginDraw | `0x00153e44` | Sets `m_LayerFlags = 8` |
| 6 | PreDraw | `0x0016bda0` | Black `flash.tex` overlay scaled by `m_Alpha` |
| 7 | Draw | `0x0014428c` | Inherited HUDControl3d::Draw (renders pause_title.tex quad) |
| 8 | PreDrawOrder | `0x0012f930` | Inherited HUDControl3d (no-op variant) |
| 9 | DrawOrder | `0x00153e98` | Conditional: only call `Draw` if `m_Alpha > 0` and not online-MP |
| 10 | Update | `0x00154468` | Main per-frame logic (see section 4) |
| 11 | SetToMultiplayerState | `0x00154060` | Swap Resume button tex to retry tex (MP fallback) |
| 12 | GetType | `0x0012f948` | Returns 1 (HUDControl3d type marker) |
| 13 | Skip | `0x0012f94c` | No-op |
| 14 | Save | `0x0012f950` | No-op |

The C++ `class` shape is single-inheritance from `HUDControl3d` only. The "second sub-vtable" blocks at `0x001e9c20+` and `0x001e9c68+` are typeinfo/RTTI siblings, not separate base subobjects.

## 4. Update body — state machine

Pre-switch: lazily create three buttons (ResumeP1 0x98, QuitP1 0xa0, RetryP1 0xac), each `MenuButton::MenuButton(SmartPtr<Texture>*, Vec3 pos, Delegate0* onPress, int=-1, Vec3 size, Delegate0* onSecondary)` allocated with `new(0x15c)`. After construct, `(*vtable[2])` initializes each, layer flag set to `0x100`, then `HUD::AddControl(game.hud, btn, false)`. `HUDControl::SetSingular(btn)` is called for the Resume button (exclusive hit detection). If `IsSameScreenMultiplayer()` is true, three more buttons are created (P2 Resume 0x9c, P2 Quit 0xa4, P2 Retry 0xb0).

**Button positions** (centered ortho, X=+160 top to -160 bottom, Y=-240 left to +240 right):
| Button | Position (x,y,z) | Size | Texture |
|---|---|---|---|
| P1 Resume (0x98) | (240, -160, 0) | scaled by `m_PauseButtonTex` size * 1.0 | pause_button / play_button (swap) |
| P1 Quit (0xa0)   | (0, 320, 0) prelim → repositioned | quit_title size | quit_title.tex |
| P1 Retry (0xac)  | (0, 320, 0) prelim → repositioned | retry_button size | retry_button.tex |
| P2 Resume (0x9c) | (240, -160, 0) | same as P1 | pause_button / play_button |
| P2 Quit (0xa4)   | -- | quit_title | quit_title.tex |
| P2 Retry (0xb0)  | -- | retry_button | retry_button.tex |

Layer flag for each MenuButton is set to **0x100** (writes to `btn+0x34`).

State machine on `m_State` (at `0xd4`):

| State | Name | Transitions / behavior |
|---|---|---|
| 0 | Hidden / idle | Per frame: `m_Alpha *= 0.75`; clamp to 0 if `< 0.01`. Decrement `m_RevealTimer -= dt`; when `<= 0`, reset to 0; once reveal timer reaches 0, re-enable Resume button (sets `Resume.field+0x131 = 1`). |
| 1 | Bomb-flash retry | Forces `m_ButtonFadeAlpha = 0`, `m_Alpha = 1.0`. Polls `BombFlashFull()` (true when global bomb flash >= 1.0). When true: clear `m_Alpha = 0`, `m_ButtonFadeAlpha = 1.0`, `PowerUpManager::GetInstance()->Reset(false)`, set `m_State = 0`, write -1.0 to `g_Game+0xc`. |
| 2 | Entry fade-in | `m_Alpha += (1.0 - m_Alpha) * 0.25` (ease-out). If not online MP, force `g_TaskState+0x02 = 1` (paused flag — see section 5). When `m_Alpha > 0.999`: state := 3, snap `m_Alpha = 1.0`. |
| 3 | Active menu | Enable hit detection on Resume (0x98) and Retry (0xac) buttons (`btn+0x131 = 1`). Continuously assert `g_TaskState+0x02 = 1` (paused) when not online. |
| 4 | Resume exit-fade | (shares code with 5/6) `m_Alpha *= 0.75`; when `< 0.001`: `m_State = 0`, `m_RevealTimer = 2.0`, `UnpauseGame()`. |
| 5 | Retry exit-fade | Same fade. When `< 0.001`: `SaveCurrentData(false)`, snap `m_Alpha = m_ButtonFadeAlpha = 0`, `m_State = 0`, `m_RevealTimer = 2.0`, `RetryLevel()`, `UnpauseGame()`. |
| 6 | Quit confirm exit | Pre-fade: extra `m_Alpha *= 0.5` once entering. Then `m_Alpha *= 0.75`; when `< 0.001`: `QuitToMenu()`. If `m_LastHitButton >= 0`: read `Vec3` from `(&m_QuitButton)[m_LastHitButton] + 8` (button pos), call `HitMenuBomb(pos)`. Then snap `m_ButtonFadeAlpha = 0`, `m_LastHitButton = -1`, `m_State = 1`, `m_Alpha = 1.0`, `SaveCurrentData(false)`, `UnpauseGame()`. |

After the switch, two more pieces of math run unconditionally:

1. **m_ButtonFadeAlpha decay**: gated on `IsEnabled(this)`. If enabled, `m_ButtonFadeAlpha *= 0.75` clamping to 0; if not enabled, lerp toward 1.0 by 0.25.
2. **State 6 special-case**: snap `m_Alpha = 1.0`, `m_ButtonFadeAlpha = 0`.
3. **Resume button texture swap** based on `m_Alpha`:
   - `m_Alpha <= 0.5`: show `m_PauseButtonTex` (or `m_QuitTitleTex` in online MP fallback).
   - `m_Alpha >  0.5`: show `m_PlayButtonTex` (resume icon).
4. **Title slide-in** (HUDControl3d.pos):
   - `pos.x = 0`
   - `pos.y = 240.0 + sizeY + (-2.0 * m_Alpha)` (slides into screen)
5. **Quit button position** (only when `m_Alpha > 0.01`):
   - `quit.field+0xc (Y) = -((240.0 - quitSizeY*0.5 - 5.0) + (1.0 - m_Alpha) * (quitSizeY + 10.0))`
   - `quit.field+0x08 (X) = 0.0 - quitSize.x * 0.5` (centered)
   - Active iff `m_PressIndex < 2`
6. **Retry button position**:
   - Position written as `Vec3(buttonOriginX*0.5 + DAT_00155218, -20.0, 0.0)`
   - `Resume.field+8 = -(buttonOriginPos + sizeY*-0.375 + 4.0 + |m_ButtonFadeAlpha| * (buttonOriginX*0.75 + 10.0))`
   - Resume scale: `local_64 = m_Alpha * 1.25 + 0.75` (button scales up while paused)
   - When `m_Alpha > 0.0`, copy Resume's screen-pos to Retry; in non-online-MP also lerp Retry pos by `(1 - m_Alpha)`. Set Retry active iff `m_Alpha > 0`.
7. **MP P2 buttons forcibly inactive** (the inherited HUD::AddControl mechanism handles them; PauseScreen sets active=false here unconditionally for `m_P2ResumeButton`/`m_P2RetryButton`).

The math constants (resolved DATs):
- `DAT_00154fb4 = 0.01` (lower fade clamp, state 0)
- `DAT_00154fb8 = 0.0`
- `DAT_00154fbc = 0.999` (state 2->3 threshold)
- `DAT_00154fc0 = 0.001` (states 4/5/6 exit threshold)
- `DAT_00154fc4 = -2.0` (title slide-in Y multiplier)
- `DAT_00154fc8 = 0.0`, `DAT_00154fd0 = 240.0`
- `DAT_00154fcc` = GOT entry to game state ptr (writes to `game+0x02 = paused flag`)
- `DAT_001543ec = 0.0` (SFX volume passed to GameSound::SFXPlay — engine treats 0 as "use default" for the panning slot in this codepath; actual amplitude is the next arg = 1.0)
- `DAT_00155218 = 0.0` (retry x-offset additive — confirmed read 32-bit float at 0x155218; full value not 0, see asm-inspector if precise)

## 5. Engine pause flag / WaveManager interaction

PauseScreen toggles **two** flags in concert:

1. **Game pause flag** at `g_Game + 0x02` (one byte). States 2 and 3 force this to `1` every frame (when not online MP). State 4/5/6 do NOT clear it directly — `UnpauseGame()` does (sets `game.timer = DAT_00168fcc`, `game+0x0c = 1`).
2. **WaveManager dt**: `QuitToMenu()` calls `WaveManager::ResetGlobalDt(1.0)` to thaw the wave timer before menu-jump. `RetryLevel()` does the same.

PauseScreen does **not** call `WaveManager::Pause`. The `g_Game+0x02` byte gates the per-frame Update loop globally — when set, Update on actor manager / wave manager is bypassed at the top-level dispatcher (see `docs/engine/wavemanager-deep-re.md` for the corresponding read site). So pausing happens by toggling that byte, not by calling a WaveManager function.

`PauseGame()` (`0x00168f80`):
```
*(byte*)(game + 0x02) = 1;
*(byte*)(game + 0x0c) = 0;
*(float*)(game + 0x08) = 0.25;   // 0x3e800000
```

`UnpauseGame()` (`0x00168fb0`):
```
*(undefined4*)(game + 0x10) = DAT_00168fcc;  // restore timer
*(byte*)(game + 0x0c) = 1;
```

## 6. Menu items / button delegates

Each `MenuButton` ctor takes two `Delegate0<void>` callbacks. The first is the press-action (built via `Mortar::Delegate0<void>::QCallee<PauseScreen>`); the second is a release/secondary delegate (built via `MakeDelegate_ShopScreen`, which in this codebase is a generic empty delegate factory — for PauseScreen the secondary slot is unused).

Resolved press-action targets (looked up via GOT entry at the listed DAT offset):

| Button | Field | Texture | Press delegate | Behavior |
|---|---|---|---|---|
| P1 Resume / Pause | 0x98 | pause_button / play_button | `0x001542d0` PauseGameCallback | State 0 -> 2 (pause from gameplay); State 3 -> 4 (resume) |
| P2 Resume / Pause | 0x9c | (same) | `0x00154400` PauseGameCallback2 | Same as P1 but flags `m_PressIndex = 1` |
| P1 Quit | 0xa0 | quit_title | `0x00153ebc` QuitGameCallback | State 3 -> 6 (quit confirm); set `m_LastHitButton = 0`; clear save totals |
| P2 Quit | 0xa4 | quit_title | (MP only — same QuitGameCallback) | |
| P1 Retry | 0xac | retry_button | `0x00154400` PauseGameCallback2 | State 3 -> 4 (via PauseGameCallback) then `m_PressIndex = 1` -> next-state machine reads as Retry path -> state 5 |
| P2 Retry | 0xb0 | retry_button | (same) | |

There are **no** "Sound on/off / Music on/off" toggle buttons on PauseScreen in this build. The original prompt asked about them; they don't exist as MenuButton instances on this screen. Sound mute is handled elsewhere (likely `OptionsScreen` / settings; see `docs/sound.md`).

### Asset names

All resolved from GOT-relative literal pointers in the ctor (GOT base = `0x001ec130`):

| Slot | DAT | String addr | Asset |
|---|---|---|---|
| `m_SecondaryTex` (0x74) | DAT_00155680 | 0x001bbe2c | `pause_title.tex` |
| `m_PauseButtonTex` (0xa8) | DAT_00155684 | 0x001bbe3c | `pause_button.tex` |
| `m_RetryButtonTex` (0xc0) | DAT_00155688 | 0x001bbe4d | `retry_button.tex` |
| `m_PlayButtonTex` (0xb8) | DAT_0015568c | 0x001bbe5e | `play_button.tex` |
| `m_QuitTitleTex` (0xbc) | DAT_00155690 | 0x001bbe6e | `quit_title.tex` |
| PreDraw flash overlay | DAT_0016becc | 0x001bc7e9 | `flash.tex` |

Loaded via `Mortar::TextureManager::LoadLocalisedTexture` (string-key-lookup that may localise per-language, but no localisation hash table present for these specific keys).

SFX strings:
- Pause: `"Pause"` at `0x001b9725` — played in PauseGameCallback when entering state 2.
- Unpause: `"Unpause"` at `0x001bbe24` — played in PauseGameCallback when entering state 4.
- Bomb-hit (state 6 quit confirm): a separate SFX (resolved by `HitMenuBomb` via DAT_0016b2d8 — "MenuBomb" / "menu_bomb"; see `docs/engine/menubutton-138.md` for full menu-bomb SFX).

### No localisation lookup

The pause / play / quit / retry textures are loaded as **textures**, not text. No `.fnt`-rendered label keys are used by PauseScreen. (The "Resume", "Restart", "Quit Game" wording is baked into the texture art at build time.)

## 7. State-transition function

There isn't a single dedicated "OnButtonPressed" dispatcher. The state graph is driven cooperatively by:

- The three button delegates (PauseGameCallback / PauseGameCallback2 / QuitGameCallback) — each is the press-action of one button. They mutate `m_State` and (for Pause/Resume) toggle the global game pause byte.
- `Update()` itself drives all timed transitions (state 2 fade-in completion, state 4/5/6 fade-out completion, state 1 bomb-flash poll).

The state graph:

```
                  +------------------ state 0 (hidden) ------------------+
                  |                                                      |
       [Resume btn pressed] (PauseGameCallback) ------------------> state 2 (entry fade)
                                                                          |
                                                          [m_Alpha > 0.999]
                                                                          v
                                                              state 3 (active menu)
                                                              /     |       \
                                            [Resume]   [Retry]    [Quit]
                                          PauseGameCb  PauseGameCb2  QuitGameCb
                                                /        |             \
                                          state 4    state 4 + idx=1    state 6
                                          (resume    (retry exit)       (quit confirm exit)
                                          exit)
                                                |        |                 |
                                                v        v                 v
                                          UnpauseGame  RetryLevel        QuitToMenu + HitMenuBomb
                                                                            + state := 1
                                                                            v
                                                                   state 1 (bomb flash poll)
                                                                            |
                                                                  [BombFlashFull()]
                                                                            v
                                                                   state 0 (hidden)
```

Note that Retry and Resume both transition through state 4 first (via PauseGameCallback). The differentiation is via `m_PressIndex` (set to 1 by PauseGameCallback2), which the state-4 exit branch reads — the decompiler shows the branch on `(m_State == 5)` for retry; this means **PauseGameCallback2 must also flip m_State to 5** (not 4), but the decompilation only shows `m_State = 4` in PauseGameCallback. This needs ASM verification — likely ARM-decompile artefact. **Gap flagged for asm-inspector**: confirm PauseGameCallback2's exact mutation of `m_State` (4 vs 5) by disassembling 0x00154400.

## 8. Touch / OnTouch

There is **no** `OnTouch` virtual on HUDControl3d in the override set; touch is dispatched by HUD core to MenuButtons via the `m_LayerFlags` bitmask intersection with the active touch layer. PauseScreen owns no direct touch-receiving slot of its own — the buttons are the touch targets. PauseScreen's `m_LayerFlags = 8` is the **render** layer, not the touch layer; buttons get layer `0x100`.

Touch hit detection on the Resume button is gated by `Resume.field+0x131` (a byte flag) which Update writes:
- State 0: `Resume.0x131 = 1` once the reveal timer expires. (Allows triggering pause from gameplay HUD.)
- State 3: `Resume.0x131 = 1` AND `Retry.0x131 = 1`. (Active menu — both interactable.)
- States 1/2/4/5/6: `0x131` not set explicitly (falls to whatever `SetActive` left it).

## 9. Dependencies / unported helpers

Already ported (per `src/`):
- `HUDControl3d` (base) — `src/hud/HUDControl3d.h`
- `MenuButton` — exists; size 0x15c. Confirm ctor signature accepts two Delegate0<void>'s — see `docs/engine/menubutton-138.md`.
- `HUD::AddControl` — implemented.

Required for full PauseScreen port:
- **MenuButton::SetSingular** — needs to mark this button as the exclusive hit recipient on its layer when active.
- **MenuButton field+0x131 (m_bInteractable)** — single-byte enable flag, driven from PauseScreen Update.
- **MenuButton field+0x125 (m_ScreenPos Vec3)** — read for `m_ButtonOriginPos` cache.
- **GameSound::SFXPlay** with named SFX "Pause" / "Unpause" — verify mappings.
- **HitMenuBomb(Vec3 pos)** — spawns a menu-bomb visual at the button position; must be present.
- **PauseGame()** / **UnpauseGame()** — global pause-byte toggles. Verify port already has these (or stubs).
- **QuitToMenu()** — does WaveManager dt reset, MainScreen state changes, NetworkManager kick. Likely needs partial implementation.
- **RetryLevel()** — resets per-fruit timers, plays game-start SFX. Needs partial implementation.
- **SaveCurrentData(false)** — save game on exit.
- **PowerUpManager::Reset(false)** — present.
- **BombFlashFull()** — predicate over `g_Game+0x10` (bomb flash phase). Verify presence.
- **flash.tex** — must exist as a converted asset (full-screen white quad used as a tinted dim layer).
- **TextureManager::LoadLocalisedTexture** — string-key lookup; port should load by basename.

## 10. Not-yet-RE'd items / asm-inspector gaps

- **PauseGameCallback2 (0x00154400)** state mutation: confirm whether it sets `m_State = 4` or `5` after wrapping PauseGameCallback. Decompiler shows only the wrapping; the differentiating branch in Update (`if (m_State == 5)` for retry) implies PauseGameCallback2 must rewrite to 5. Suggest asm-inspector pass on `0x00154400`.
- **DAT_00155214 / DAT_00155218 / DAT_0015521c** — Z constants (likely 0.0) and retry-button X tween constants. Need precise float reads to verify exact retry-position math used in MP layout. Non-blocking for Tier-1.
- **Resume.field+0x131 vs SetActive interaction** — the flag is touched both by Update directly and by MenuButton::SetActive(true) elsewhere. Confirm MenuButton ctor zero-inits this field (likely via vtable[2] = the layer-flags init, which is called right after construction).

---

## Tier-1 implementer action list (visible menu first)

Goal: get the user-visible pause overlay rendering correctly, with the three buttons (Resume / Quit / Retry) clickable and the correct state transitions for single-player.

1. Replace stub fields in `src/screens/PauseScreen.{h,cpp}` with the layout in section 2. Verify offsets with `static_assert(offsetof(...) == ...)` for every field 0x7c..0xd4.
2. **ctor**: load 5 textures (`pause_title.tex`, `pause_button.tex`, `play_button.tex`, `quit_title.tex`, `retry_button.tex`); compute initial `pos` from title size; init `m_State=0`, `m_Alpha=0`, `m_ButtonFadeAlpha=1`, `m_RevealTimer=0`, `m_LastHitButton=-1`, `m_LayerFlags=8`.
3. **Init() (vtable[2])**: forward to `Reset()` (i.e. `HUDControl::Reset(this)`).
4. **BeginDraw() (vtable[5])**: assert `m_LayerFlags = 8`.
5. **PreDraw() (vtable[6])**: implement the `flash.tex` black-tinted overlay. Alpha = `clamp(m_Alpha * 1000.0, 0, 128)`; tint = (0,0,0,alpha). Scale = `m_Alpha * 10000.0` (covers full screen at any aspect).
6. **DrawOrder() (vtable[9])**: call `HUDControl3d::Draw` only when `m_Alpha > 0` and not online MP.
7. **Update()**: implement Tier-1 SP path only (skip the `IsSameScreenMultiplayer()` branch). Lazily create the three P1 buttons with correct positions/sizes and bind delegates to PauseGameCallback / QuitGameCallback / PauseGameCallback2. Drive states 0->2->3->{4|5|6}->{0|1->0}.
8. **PauseGameCallback / PauseGameCallback2 / QuitGameCallback**: implement as plain methods. Verify Resume button toggles between `m_PauseButtonTex` and `m_PlayButtonTex` based on `m_Alpha <= 0.5`.
9. Wire `PauseGame()` / `UnpauseGame()` to actually toggle the global game-pause byte. Confirm it's already represented in the ported `g_Game` struct.

## Tier-2 (full fidelity)

10. Same-screen MP P2 button tree (0x9c / 0xa4 / 0xb0).
11. Online-MP visibility branches (Quit hidden, layout shifted, DrawOrder skipped).
12. State 1 bomb-flash dependency (`BombFlashFull()`); requires bomb-flash effect ported.
13. State 6 `HitMenuBomb` at quit button position.
14. `MenuButton::SetSingular` exclusive-hit semantics.
15. Retry slide-in lerp (`(1 - m_Alpha)` blending of retry button position toward Resume button position).
16. Reveal timer (`m_RevealTimer = 2.0` after exit; counts down; gates Resume button re-enable). This produces the "1-second grace before the pause button is responsive again after retry/quit/resume".
17. asm-inspector gap from section 10.
