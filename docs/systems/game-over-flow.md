# Game-Over Flow System

<!-- Analysed: 2026-04-30T12:00 -->

High-level architecture of the game-over trigger system, state transitions, field management, and port-specific caveats.

## Core GameOver Implementation

**Function:** `GameOver(int endReason, float endScore, int endParam)` at **0x00169ed4**

### GameOver Pseudocode

```c
void GameOver(int endReason, float endScore, int endParam) {
    if (g_GameData.pauseFlag /*+0x05*/ == 0) {
        g_GameData.pauseFlag = 1;
        WaveManager::ClearUnspawned(WaveManager::GetInstance());
        GameOverScreen* gos = (GameOverScreen*)operator_new(0x13c);
        FruitSaveData* sd  = g_GameData.pSaveData;
        GameOverScreen::GameOverScreen(gos, "GameOver", endReason, endScore,
            sd[0x120], sd[0x11C], sd[0x124], sd[0x128]);
        g_GameData.pGameOverScreen /*+0x164*/ = gos;
        sd[0x120] = sd[0x128] = sd[0x124] = sd[0x11C] = -1;
        if (endReason == -1) {
            // FruitSaveData::AddToTotal("GamesPlayed-..."), unique-day tracking
        }
        gos->vtable->Init(gos);
        HUD::AddControl(g_GameData.pHUD, gos, false);
    }
}
```

**Key detail:** `pauseFlag` at `g_GameData+0x05` is the **single guard** — when set to 1, all further GameOver calls are skipped (double-call protection).

## GameOver Trigger Points (6 Callers)

### 1. Fruit::KillFruit (Classic Mode Miss) — 0x00176d20

**Trigger:** 3rd miss detected in Classic mode

```c
if (++missCount > 2) {  // missCount 0, 1, 2 allowed; 3rd triggers GameOver
    GameOver(-1, -1.0f, -1);
}
```

### 2. Fruit::CheckFruitDropped (Classic Same-Screen MP) — 0x001761c2

**Trigger:** Per-player miss counter exceeds threshold in same-screen multiplayer

Identical guard: `++missCount > 2` but per player.

### 3. TimeControl::Update (Arcade/Zen Timeout) — 0x001625be

**Trigger:** `m_TimeRemaining < 0.5f` when time expires

Guard: `pauseFlag == 0` checked before calling GameOver.

### 4. WaveManager::ReachedEnd (Any Mode) — 0x001234f4

**Trigger:** Wave queue exhausted (all pre-spawned fruit/bomb entities consumed)

Called regardless of game mode. Guards the entire wave progression.

### 5. GameUpdate Bomb-Hit (Any Mode) — 0x0016c284

**Trigger:** `bombHitTimer` crosses 1.5 downward (delay after bomb hit expires)

```c
if (bombHitTimer > 1.5f && prev <= 1.5f && !pauseFlag && !m_bMenuBombHit) {
    GameOver(...);
}
```

This is the "bomb miss" trigger — bombs are uncatchable (instant game-over).

### 6. SkipToGameOver (Debug) — 0x0016ae0e

**Trigger:** Debug function to fast-skip to game-over state

Signature: `void SkipToGameOver(int reason, float score, float x, float y, int param)`

**Port note:** Present in binary but likely disabled in shipping build.

## Game-Over Guard Field

**Field:** `pauseFlag` at `g_GameData+0x05`

**Dual purpose:**
1. **Pause state:** Set by `PauseGame()`, cleared by `UnpauseGame()`
2. **Game-over gate:** Set by `GameOver()` to 1, prevents re-entry

There is **NO separate `gameOverFlag`** — `pauseFlag` serves both roles.

| Value | Meaning | Set by | Cleared by |
|-------|---------|--------|-----------|
| 0 | Active gameplay | GameModeScreen mode-pick transition; EndRetryLevel; SetupGameWork; MainScreen state 0x11 | —— |
| 1 | Game paused OR over | GameOver; PauseGame; MainScreen state 2 (quit); QuitToMenu | PauseGame; EndRetryLevel; SetupGameWork; GameModeScreen transition 3-6; MainScreen state 0x11 |

**Flow:** GameModeScreen transition (0x0013f2bc) clears pauseFlag → gameplay active. Any of 6 triggers → pauseFlag=1 + HUD GameOverScreen added. GameOverScreen dtor → cleaned up via HUD removal.

## Retry Flag & EndRetryLevel Flow

**Field:** `retryFlag` at `g_GameData+0x06`

| Caller | Sets | Clears | Purpose |
|--------|------|--------|---------|
| GameOverScreen::RetryButtonCallback | 0x06 → 1 | —— | Signal retry intent |
| EndRetryLevel (0x0016a25c) | —— | 0x06 → 0 | Completion of retry sequence |
| QuitToMenu (0x00169e50) | —— | 0x06 → 0 | Abort retry on quit |
| SetupGameWork (0x0010b4e8) | —— | 0x06 → 0 | Init new game |

**EndRetryLevel implementation:**
- Called from GameUpdate when `retryFlag == 1`
- Resets all wave, entity, and score state
- Calls `WaveManager::Reset(true)` — the **only legitimate use** of the true parameter
- Clears `retryFlag`
- Clears `pauseFlag` → gameplay resumes

## MainScreen State Machine & Mode Pick

**State 0x11 (CAMERA_FADE)** at 0x0014c0fa

This is the **post-mode-pick transition state** — NOT a reset.

```c
if (g_GameData.cameraTransition < 0.0) {
    g_GameData.cameraTransition *= 0.75;
    if (g_GameData.cameraTransition >= 0) {
        g_GameData.cameraTransition = 0.0;
        g_GameData.pauseFlag = 0;
    }
}
```

**Critical:** State 0x11 does **NOT** call `WaveManager::Reset()`.

### GameModeScreen Mode-Pick Callbacks

| Callback | Address | m_State | gameMode |
|----------|---------|---------|----------|
| ClassicModeCallback | 0x0013dfb4 | 3 | 0 |
| ZenModeCallback | 0x0013dffc | 6 | 3 |
| ArcadeModeCallback | 0x0013e19c | 5 | 2 |

**Transition sequence** (GameModeScreen::Update case 3..6, 0x0013f2bc):

```c
m_TransitionAlpha *= 0.75;
m_field24_0xb4    *= 0.75;
g_GameData.cameraTransition *= 0.75;
if (Math::Abs(g_GameData.cameraTransition) < 0.001) {
    GameSound::SFXPlay("Game-start", ...);
    g_GameData.cameraTransition = 0.0;
    g_GameData.pauseFlag = 0;
    this->m_bPendingRemoval = 1;
    g_GameData.pMainScreen->m_State = 0x11;   // CAMERA_FADE
}
```

Mode pick **just changes `gameMode`** and transitions to MainScreen state 0x11 (fade-in). **The wave continues running** — it is **not reset** here.

## Dead Code: Never Dispatched

### 1. Game::TellGameToStart (vtable[10])

**Address:** 0x0010dc80

**Status:** vtable slot exists but is **never dispatched** from anywhere in the codebase.

```c
// Pseudo: Game::TellGameToStart(int param) {
//     HUD::SetToMultiplayerState(g_GameData.pHUD);
//     WaveManager::Reset(true);
// }
```

**Port note:** Do NOT re-implement this function. It is dead code. Wave initialization happens in `GameInit` + `WaveManager::Resume`.

### 2. GameModeScreen::SetupLevel (vtable[18])

**Address:** 0x0013f274 (SetupLevel entry), calls PrepareForLevelStart (0x00169ab4)

**Status:** vtable slot exists but is **never dispatched** from anywhere.

**Calls:** `PrepareForLevelStart()` → `WaveManager::Reset(false)` (0x00125be4)

**Port note:** Do NOT re-implement this. It is dead code. All level setup happens via `GameInit` and mode-pick transitions.

## Wave Architecture

### Continuous Running Model

The wave runs **continuously from startup** — not reset per mode-pick:

1. **GameInit** (0x0016c644)
   - Creates `WaveManager` singleton
   - Calls `WaveManager::Init()` — loads wave XML, initializes spawn queue
   - Pre-spawns **30 fruit/bomb entities** via `WaveManager::Resume()`
   - Wave is now **actively spawning**

2. **Mode pick** (e.g., ClassicModeCallback)
   - Changes `g_GameData.gameMode` (0-3)
   - Does **not** call `WaveManager::Reset()`
   - Wave continues spawning at current position

3. **GameModeScreen transition** (state 3..6)
   - Fade-in animation
   - When fade complete, sets `g_GameData.pauseFlag = 0` and MainScreen state = 0x11
   - Gameplay resumes with the running wave

4. **WaveManager::ReachedEnd()**
   - Called from `GameUpdate` when spawn queue is exhausted
   - Triggers `GameOver(-1, -1.0f, -1)` (wave-exhaustion loss)

### Wave Reset Callers (4 legitimate uses)

| Caller | Address | bool arg | Purpose |
|--------|---------|----------|---------|
| EndRetryLevel | 0x0016a25c | true | Retry restart: reset wave, clear entities, respawn 30 |
| GameOverScreen::Update state 8 | 0x00141ea2 | false | Quit-to-menu: reset without respawn |
| GameOverScreen::Update state ? | 0x00141f2a | false | Retry restart variant |
| MainScreen::Update state 2 | 0x0014ba12 | true | Menu-side in-game start (NewGameCallback path) |

**Note:** Only the two `true` cases (EndRetryLevel, MainScreen state 2) actually spawn entities. The `false` cases just reset timing state.

## Field Summary Table

| Field | Offset | Set by | Cleared by | Read by | Purpose |
|-------|--------|--------|-----------|---------|---------|
| pauseFlag | g_GameData+0x05 | GameOver, PauseGame, QuitToMenu, MainScreen st.2 | EndRetryLevel, SetupGameWork, GameModeScreen transition, MainScreen st.0x11 | GameOver guard, TimeControl::Update, GameUpdate bomb-check, Fruit::KillFruit | Pause AND game-over gate |
| retryFlag | g_GameData+0x06 | GameOverScreen::RetryButton | EndRetryLevel, QuitToMenu, SetupGameWork | GameUpdate (RetryUpdate dispatch) | Retry state signal |
| pGameOverScreen | g_GameData+0x164 | GameOver | SetupGameWork (null), GameOverScreen dtor (via HUD removal) | HUD lifecycle only | Pointer to active game-over screen |

## Port-Specific Caveats

### Workaround: WaveManager::Reset Before Fade (DIFFERS)

The port currently calls `WaveManager::Reset(true)` in `GameModeScreen.cpp` before transitioning to `STATE_CAMERA_FADE`. 

**Port specific:** This is a workaround to avoid wave desync during development. 

**Correct behavior (binary):** The wave is NOT reset on mode-pick. If the port observes incorrect wave spawn behavior on mode-pick, the bug is in **startup wave initialization** (`GameInit` / `WaveManager::Resume()`), not in a missing reset.

**Action:** Once startup wave init is complete and verified, this workaround should be removed and the mode-pick path should be left alone.

### No Separate gameOverFlag

The port must **not** introduce a separate `gameOverFlag` field. The binary uses `pauseFlag` for both pause and game-over states. Any port-side separation will drift from the original behavior.

### gameMode Continuity

Mode pick (Classic/Zen/Arcade) sets `g_GameData.gameMode` but does **not** reset game state beyond clearing pauseFlag. All gameplay logic (scoring, time limit, miss counter) reads gameMode at runtime to adjust behavior.

## See Also

- [state-machine.md](state-machine.md) — Task state machine, state transitions
- [wave-system.md](wave-system.md) — WaveManager architecture, wave XML format
- [structs/game.md](../structs/game.md) — g_GameData layout, pauseFlag field
- [structs/hud.md](../structs/hud.md) — TimeControl struct (countdown timer)
- [screens/game-over.md](../screens/game-over.md) — GameOverScreen UI, retry/quit flow
- [screens/game-mode.md](../screens/game-mode.md) — GameModeScreen mode selection callbacks
