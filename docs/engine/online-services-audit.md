# Online Services Stub Audit

Analysed: 2026-05-02

Confirms that the four defunct online-service stubs (OpenFeint / GameCenter /
P2P) in the desktop port have no live calls in the gameplay loop, and that
all current references are either comments, init-time, or already routed to
no-op stubs with matching signatures.

Per `CLAUDE.md` Port Goal: "Skip only defunct online services (OpenFeint,
GameCenter, P2P multiplayer)." The port does not link any online SDK and
does not need to.

## 1. Inventory of Stub Classes

| Class | File | Size (bytes) | State |
|-------|------|--------------|-------|
| `AchievementManager` | `src/game/AchievementManager.h` | 2756 | Header-only; all methods inline no-ops; `AchievementExists()` returns 0; binary addresses documented |
| `LeaderboardManager` | `src/game/LeaderboardManager.h` | 1335 | Header-only; ctor zero-fills 4 ulongs to mimic 0x40-byte binary footprint; `Refresh/UpdateLeaderboard` no-op |
| `Mortar::NetworkManager` | `src/engine/network/NetworkManager.h` + `.cpp` | 2313 + 214 | Header + empty .cpp; padded to 668 bytes; 9 no-op methods (Init, Destroy, P2PConnect, IsOnlineMultiplayer=false, AreGameCenterConnectionAttemptsAllowed=false, etc.) |
| `LeaderboardScreen` | `src/screens/LeaderboardScreen.h` | 329 | Stub struct with `LoadContent()` / `UnLoadContent()` no-ops |

`EntityTracker` (mentioned in the task brief as P2P-only) is not present in
`src/` — there is no port stub or call. References exist only in `docs/` and
`function_hash_index.json`. No action needed.

## 2. Caller List per Class

### 2.1 AchievementManager

| Caller (port) | Path | Classification |
|---|---|---|
| `ItemManager::LoadItemData` | `src/game/ItemManager.cpp:121-122` | **gated/init-time** — runs once at startup during XML load. Calls `GetInstance()` then `AchievementExists()`; with stub returning 0 the achievement-gated items follow the documented "new item / free" branch (see `docs/structs/items.md`). Not a gameplay-loop call. |
| `FruitSaveData.cpp:159` | comment-only TODO note | **dead** — text reference, not a call. |
| `GameInitialise.cpp:184, 357` | comment-only TODOs | **dead** — text references. |

Binary callers that are NOT yet wired in port (potential fidelity gaps, not
"live call" risks for the stub):

| Binary call site | Port equivalent | Status |
|---|---|---|
| `Fruit::CollisionResponse @ 0x001780b0` -> `UnlockConsecutiveAchievement` | `src/entities/Fruit.cpp::CollisionResponse` | port omits the call entirely |
| `WaveManager::GetNextWave @ 0x00124f10` -> `UnlockTotalFruitAchievement` | `src/game/WaveManager.cpp::GetNextWave` | port omits the call entirely |
| `GameOverScreen::Update @ 0x00141b34` -> `UnlockTotalFruitAchievement` | `src/screens/GameOverScreen` | port omits the call entirely |

These are **fidelity gaps**, not stub-safety concerns: even when the port
eventually calls them, the existing inline no-op stubs absorb the call
safely. If/when the achievement notification UI is ported, only the stub
bodies need filling — the call sites will resolve through the existing
header.

### 2.2 LeaderboardManager

| Caller (port) | Path | Classification |
|---|---|---|
| `GameInitialise.cpp:217, 341` | comment-only TODOs | **dead** — text references. |

No live callers. Binary callers (`FruitFactControl::Update @ 0x0013b604`,
`RefreshLeaderboard` thunks) are not wired in the port. Same fidelity-gap
note as Achievements: stubs already absorb any future call.

### 2.3 Mortar::NetworkManager

| Caller (port) | Path | Classification |
|---|---|---|
| `PowerUpManager.cpp:133` | comment-only TODO (`(*game->m_pNetMgr->vtable[4])() - SyncClear`) | **dead** — inside a `fullReset` branch, the line is commented out. |
| `DojoScreen.cpp:325-327` | state-4 dashboard branch | **dead** — port code resets `m_State = 0` and the `LaunchDashboard()` call is documented but not invoked. |
| `MainScreen.cpp:358-374` | STATE_LEADERBOARD / STATE_MORE_GAMES / STATE_MATCHMAKER / STATE_NEWS | **dead** — these states transition straight back to STATE_CAMERA_ZOOM / STATE_CREATE_BUTTONS without invoking any NetworkManager method. |
| `PauseScreen.cpp:112` | comment in `QuitToMenu()` | **dead** — text reference; no call. |
| `AboutScreen.cpp:297-316` | OFN button creation block | **dead** — `if` body is a `(void)POS_OFN_BUTTON` no-op with TODO. |
| `engine/CMakeLists.txt:43` | build wiring | **build-only** — links empty `.cpp`. No symbol references. |

No live callers anywhere in `src/`. Even the singleton is never accessed
because no caller does `NetworkManager::GetInstance()`. The class exists
purely as a documented reference / future hook.

### 2.4 LeaderboardScreen

| Caller (port) | Path | Classification |
|---|---|---|
| `GameInitialise.cpp:354` | `LeaderboardScreen::UnLoadContent()` | **live, no-op** — fires once on `GameDestroy`. The stub method body is empty. Safe. |

Single live caller; the call resolves to a no-op with no side effects.

## 3. Verdict per Class

| Class | Verdict |
|---|---|
| `AchievementManager` | **safe to leave stubbed** — only init-time call (`AchievementExists` from `ItemManager::LoadItemData`) returns 0 by design and is documented in `docs/structs/items.md`. Gameplay-path binary callers are not yet wired in the port; when they are, no further stub work is required. |
| `LeaderboardManager` | **safe to leave stubbed** — zero live callers in port; comments only. Stub matches binary layout (0x40-byte zero-fill) and signatures. |
| `Mortar::NetworkManager` | **safe to leave stubbed** — zero live callers; even `GetInstance()` is never invoked. The header documents binary addresses but no port code reaches it. |
| `LeaderboardScreen` | **safe to leave stubbed** — single live call (`UnLoadContent` on shutdown) hits a no-op body. |

## 4. Action Items

None for online-services safety. All stubs are confirmed safe with respect
to the gameplay loop and the engine-init / engine-destroy paths.

Optional follow-ups (NOT online-safety issues, separately tracked):

- When achievement notification UI is ported, fill `UnlockAchievement`,
  `UnlockConsecutiveAchievement`, `UnlockTotalFruitAchievement` bodies and
  add the missing call sites in `Fruit::CollisionResponse`,
  `WaveManager::GetNextWave`, and `GameOverScreen::Update` — track in
  `docs/TODO.md` under achievements, not here.
- `FruitFactControl::Update` currently does not invoke
  `LeaderboardManager::UpdateLeaderboard`; same fidelity-gap category.

These are fidelity TODOs, not safety bugs. The stubs themselves require no
patches.
