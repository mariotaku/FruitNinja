# Splash flow

Status: RE complete (2026-04-30). Resolves the apparent contradiction noted in
`game-over-flow.md` and the user-reported "no splash on boot."

## 1. Verdict

**The binary has a splash, but it is NOT rendered by `SplashTask` / `SplashInit` / `SplashUpdate` / `SplashDraw` / `SplashExit`.**

Those four functions are dead code in the shipped binary:

- They have **zero in-binary call sites**. The only "xrefs" Ghidra reports
  are the `Entry Point [EXTERNAL]` markers the Bada toolchain emits for
  every C++ symbol with external linkage.
- The dispatch table at `0x001E8A28` is 12 entries x 4 bytes = 48 bytes,
  laid out as 4 columns (Draw, Exit, Update, Init) x 3 rows (one per task
  state index). All three rows in every column point at `Game{Draw,Exit,Update,Init}`.
  Splash and Frontend addresses never appear in the table.
- `taskStateIndex` (g_GameData+0x00) therefore has only one effective value
  at runtime: 2 (Game). The "0=Splash, 1=Frontend, 2=Game" enum the port
  carries forward is the *source-code* enum from the original C++; the
  *runtime* enum is just {Game}.

**The actual splash lives inside `GameUpdate` / `GameDraw`** as a self-contained
1.5-second fade overlay, gated on a `.data`-initialised float timer. Asset:
`HB_logo.tex` (Halfbrick logo). No separate task, no MenuBackground, no
SplashTask. The first 1.5 s of `taskStateIndex == 2` is the splash; after that
the timer hits 0 and the same Game state runs the menu / gameplay.

This explains why removing `SplashTask` from the port's dispatch table does
not, on its own, make a splash appear -- the binary's splash was never in
the dispatch table to begin with.

## 2. Binary references

### Task-state dispatch table (0x001E8A28, 48 bytes)

Read via `read_memory`:

| Slot offset | Column          | Row 0          | Row 1          | Row 2          |
|-------------|-----------------|----------------|----------------|----------------|
| +0x00       | Draw            | `0x0016B888` GameDraw | GameDraw | GameDraw |
| +0x0C       | Exit            | `0x0016CF74` GameExit | GameExit | GameExit |
| +0x18       | Update          | `0x0016BED0` GameUpdate | GameUpdate | GameUpdate |
| +0x24       | Init            | `0x0016C644` GameInit | GameInit | GameInit |

xref to the table itself: `GameTaskDraw @ 0x0010A2C4` (the engine-level dispatcher loaded at `Game::run` time).

### Splash assets and timer

| Item | Address | Notes |
|------|---------|-------|
| `HB_logo.tex` (string) | `0x001BC8FA` | The only splash texture. `white_splash.tex` is the slash juice splat -- unrelated. |
| `Game+0xF4` (`SmartPtr<Texture>`) | runtime `0x002314F8` | Holds the loaded `HB_logo.tex`. Distinct from `Game+0xFC` which is the gameplay background. |
| `g_TaskState @ 0x001F3D84` | `.data` | Per-translation-unit state for `GameTask.cpp`. |
| `g_TaskState +0x1C` | `0x001F3DA0` | Splash fade timer, **statically initialised to 1.5f** in `.data` (bytes `00 00 c0 3f`). |
| Initial bytes at `g_TaskState` | -- | `+0x00=1.0f, +0x04=1.0f, +0x08=-10.0f, +0x0C=-500.0f, +0x10=14 (byte), +0x14=0.33f, +0x18=0.33f,` **`+0x1C=1.5f`**`, +0x20=1.0f, +0x24=1.0f` |

### `LoadingJob` (0x001F3D44, 2 bytes in `.data`)

| Field | Initial | Set by `Begin()` | Read by |
|-------|---------|------------------|---------|
| `+0x00` IsLoaded | 1 | 0 then 1 | `LoadingJob::IsLoaded()` @ `0x0012E16C` |
| `+0x01` CanBoot  | 1 | 1         | `LoadingJob::CanBoot()` @ `0x0012E184` |

`Begin()` (`0x0012E148`) writes the bytes 0,1,1 in that order. The static `.data`
initialiser already leaves both flags at 1, so on cold boot the splash branch's
"wait for assets" gate is a no-op -- the fade starts ticking immediately.

### Splash branch in `GameUpdate` (0x0016BED0)

Pseudocode (resolved DAT names, GOT base = `0x001EC130`):

```c
// === Splash phase, gated on the static-init 1.5s timer ===
if (g_TaskState.splashFadeTimer > 0.0f) {           // +0x1C, init = 1.5f
    if (!g_GameData.pSplashTex) {                   // +0xF4 SmartPtr
        g_GameData.pSplashTex = TextureManager::LoadLocalisedTexture("HB_logo.tex");
    }
    if (!LoadingJob::CanBoot()) {
        return;                                     // assets not ready -> hold splash
    }
    g_GameData.dt = 0.0f;                           // freeze game-time while fading
    g_TaskState.splashFadeTimer -= dt * 2.0f;       // 1.5s real time -> 0.75s fade
    if (g_TaskState.splashFadeTimer <= 0.0f) {
        g_TaskState.splashFadeTimer = 0.0f;
        SmartPtrNull(&g_GameData.pSplashTex);       // release HB_logo
    }
    // falls through to InputManager::Update etc.
} else {
    InputManager::GetInstance()->Update(dt);        // normal path
}
```

Note: `dt * 2.0f` halves the wall-clock duration. With dt = 1/60 the timer
drains in 1.5 / (2 * 1/60) = 45 frames = 0.75 s of wall time. (The earlier
"1.5 s" figure refers to the *value* of the timer, not the elapsed duration.)

### Splash overlay in `GameDraw` (0x0016B888) and `DrawStartFade` (0x0016AB10)

`GameDraw` calls `DrawStartFade()` in two places:

1. **Loading-screen mode** (assets not yet ready):
   ```c
   if (LoadingJob::CanBoot() == 0) {
       if (g_TaskState.splashFadeTimer <= 0.0f) return;
       DrawStartFade();
       return;
   }
   ```
   Background already drawn, no actors yet, just the splash overlay.

2. **Fade-over-game mode** (assets ready, fade still ticking down):
   ```c
   // ... full game render ...
   fadeTimer = g_TaskState.splashFadeTimer;
   if (fadeTimer > 0.0f) DrawStartFade();
   ```

`DrawStartFade()` body (constants resolved):

```c
void DrawStartFade(void) {
    float t = g_TaskState.splashFadeTimer;          // 1.5 -> 0
    if (t <= 0.0f) return;
    FruitCamera::SetupPerspective(camera, 3, 1);    // ortho/screen-space mode

    float bright, alpha_factor, rgb_factor;
    if (t <= 0.5f) {
        // Outro: alpha and white-balance both fade to 0
        bright       = t * 2.0f;                    // 0..1
        rgb_factor   = 0.0f;
        alpha_factor = 1.0f;
    } else {
        // Intro/hold: white tint ramps in, alpha stays full
        rgb_factor   = clamp((t - 0.5f) * 2.0f, 0, 1);
        bright       = 1.0f;
        alpha_factor = (1.0f - rgb_factor) * (1.0f - rgb_factor) + 1.0f;
    }
    Texture::Set(g_GameData.pSplashTex);            // HB_logo.tex
    ResetMatrixStack();
    ScaleMatrix(SCREEN_W * 1.0f, SCREEN_H * 1.0f, 0); // FADE_GAME_WIDTH/HEIGHT
    UploadMatrices();

    uint8_t a = clamp_u8(bright * alpha_factor * 255.0f);
    uint8_t r = clamp_u8(rgb_factor * 255.0f);
    Colour col(r, r, r, a);
    DrawQuad_Rect(0.03125f, 0.96875f, 0.1875f, 0.8125f, &col);  // UV crop
    Texture::UnSet(g_GameData.pSplashTex);
}
```

Visual phase summary (timer values are in seconds-of-timer, drained at 2x dt):

| Timer (game units) | Wall time | rgb_factor | alpha_factor | bright | Render |
|--------------------|-----------|------------|--------------|--------|--------|
| 1.5 -> 1.0         | 0.00..0.25 s | 1.0 -> 0.0 | 1.0 -> 2.0 (clamped at a=255) | 1 | white box, logo invisible (white tint) |
| 1.0 -> 0.5         | 0.25..0.5 s  | 0.0        | 2.0          | 1 | logo visible on white background |
| 0.5 -> 0.0         | 0.5..0.75 s  | 0          | 1            | 1 -> 0 | logo + white background fade together to transparent |

So the user-perceived splash is roughly **0.75 s of "white screen, then HB logo,
then fade out to game"**. On real Bada hardware the LoadingJob gate adds up to
several extra seconds while assets stream in.

### Why `SplashTask` exists in the binary at all

`SplashTask.cpp`, `FrontendTask.cpp` and `GameTask.cpp` look like a templated
3-state task system the engine ships with. Halfbrick's other titles likely use
all three; FruitNinja built with all three but registered only Game in the
dispatch table. The `_GLOBAL__I_SplashTask.cpp` static-init runs (initialises a
`Vector3` constant set, type IDs, etc.) but the four state functions are never
reached. Dead code. They draw nothing -- `SplashDraw` is `return;` and `FrontendDraw`
is `return;`.

## 3. Port gap

### Current behaviour (`src/`)

- `src/game/GameTaskState.cpp:21-24` registers all three state slots with
  `SplashInit / SplashUpdate / SplashDraw / SplashExit` and
  `FrontendInit / ...`. This is a faithful re-creation of the *source* table
  Halfbrick presumably had, **not** the binary's runtime table.
- `src/game/SplashTask.cpp:13-19` immediately writes
  `game->taskStateIndex = 2` from `SplashInit`, transitioning to Game on
  the first frame. `SplashDraw` is a no-op.
- `src/Game.cpp:107` sets `taskStateIndex = 0` at boot. The dispatcher then
  runs `SplashInit` (which schedules a transition), and on the next tick
  state 2's `GameInit` runs. Net effect: the player sees roughly one black
  frame, then the game.
- `g_TaskState +0x1C` (the splash fade timer) is **not modelled** in the
  port's `GameTaskState` struct (`src/game/GameTaskState.h`). The port's
  `GameUpdate` / `GameDraw` do not test this timer or call any splash-fade
  drawing code.
- The port has no `Game.pSplashTex` (`Game+0xF4`) field and no equivalent of
  `DrawStartFade()`.
- `LoadingJob` is not modelled at all in the port -- no `LoadingJob::CanBoot()`
  / `IsLoaded()` calls. (For asset-already-on-disk port targets this is
  fine; the binary's static-init defaults make the wait a no-op anyway.)
- `HB_logo.tex` is not registered or extracted as part of the asset bundle
  (TBC -- check `data/` for the converted asset).

The port currently behaves as if the binary's splash branch never ran -- because
both the SplashTask path *and* the in-`GameUpdate` splash branch are absent.

### File-level evidence

- `src/game/SplashTask.cpp:13-19` -- auto-transitions, draws nothing.
- `src/game/FrontendTask.cpp:9-13` -- same shape, also draws nothing.
- `src/game/GameTaskState.cpp:21-25` -- 3-row dispatch table; mirrors source not binary.
- `src/Game.cpp:107` -- `taskStateIndex = 0` at boot (sources Splash row).
- `src/game/GameTaskState.h:36-85` -- no `splashFadeTimer` / `pSplashTex`.
- `src/Game.h` -- no `Game::pSplashTex` (`+0xF4`); only `pBackgroundTexture` (`+0xFC`).
- No `DrawStartFade` symbol in the port.

## 4. Implementation plan

The fix is **not** to flesh out `SplashTask` -- that path is dead in the binary.
Re-create the in-`GameUpdate` / `GameDraw` overlay instead.

### Step A: extend `GameTaskState` (the per-task state, not g_GameData)

`src/game/GameTaskState.h`:
- Add `float splashFadeTimer = 1.5f;` matching `g_TaskState +0x1C`.
- Initialise to 1.5f in the `GameTaskState()` ctor (matches `.data` static init).

### Step B: extend `Game`

`src/Game.h`:
- Add `SmartPtr<Mortar::Texture> pSplashTex;` at the offset corresponding to
  `Game+0xF4`. Match the existing pattern used for `pBackgroundTexture`.
- `src/Game.cpp` ctor: leave it null; loaded on demand in `GameUpdate`.

### Step C: re-create the splash branch in `GameUpdate`

In `src/game/` (the port's `GameUpdate.cpp` -- check exact filename), at the
top of the function, before any other logic, port the pseudocode from section
2 above:

```cpp
GameTaskState* ts = GetTaskState();
if (ts->splashFadeTimer > 0.0f) {
    if (!game->pSplashTex) {
        game->pSplashTex = Mortar::TextureManager::GetInstance()
                              .LoadLocalisedTexture("HB_logo.tex");
    }
    // Port: LoadingJob is not modelled. Binary's static init has CanBoot=1,
    // so the original game also skips the wait on cold boot. Match that by
    // omitting the LoadingJob::CanBoot() gate.
    game->dt = 0.0f;
    ts->splashFadeTimer -= dt * 2.0f;
    if (ts->splashFadeTimer <= 0.0f) {
        ts->splashFadeTimer = 0.0f;
        game->pSplashTex.Reset();
    }
    // fall through; do not return -- InputManager / sound updates still need to run
}
```

### Step D: re-create `DrawStartFade` in `GameDraw`

Add a `DrawStartFade()` static (or namespace) function in the port's GameDraw
translation unit, matching the pseudocode from section 2. Plug it into
`GameDraw`:

- At the top, before drawing actors, when `splashFadeTimer > 0.0f` AND the
  port has no `LoadingJob` model: just draw the background (already happens),
  **then early-out when `splashFadeTimer > 0.0f` only if assets are unloaded**.
  Port simplification: assets are always loaded, so go straight to the
  end-of-frame overlay.
- At the end of the function (after HUD draw), match the binary:
  ```cpp
  if (ts->splashFadeTimer > 0.0f) DrawStartFade();
  ```

### Step E: remove the dead-code dispatch path (optional cleanup)

Once the in-frame splash works:

- `src/game/GameTaskState.cpp:21-25`: collapse the 3-state table to a single
  Game row, or keep it but document that Splash/Frontend rows are unreachable
  (they are, since `taskStateIndex` will start at 2). Minimum viable change:
  set `src/Game.cpp:107` to `taskStateIndex = 2` so the dispatcher matches
  the binary's runtime behaviour.
- `src/game/SplashTask.cpp` and `src/game/FrontendTask.cpp` can stay as no-op
  stubs (they're the source-level functions; preserving them documents the
  original C++ structure). Add a comment noting they are unreachable in the
  shipped binary.

### Step F: assets

Confirm `HB_logo.tex` (or the port's converted `HB_logo.png`) is in the
asset bundle and accessible via the port's `LoadLocalisedTexture` -- this
is not yet verified. If absent, the texture load returns null and the splash
quad either no-ops or renders as untextured white, neither of which is fatal
but neither matches the binary.

### Out of scope

- `LoadingJob` system. The port doesn't async-load anything in a way that
  could fail to be ready before `GameUpdate` first ticks; faithfully
  reimplementing `LoadingJob::Begin / IsLoaded / CanBoot` adds nothing.
- `MenuBackground` (used by the dead `SplashInit` path). Since the binary
  never instantiates one during the actual splash, the port shouldn't either.
- `SplatEntity::LoadContent` reference to `white_splash.tex`. Misleading
  name -- this is the slash juice splat, not the splash screen. Already
  ported elsewhere; keep as-is.
