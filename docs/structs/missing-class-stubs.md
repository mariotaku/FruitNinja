# Missing Class Stubs — Spec for No-Op Implementations

RE'd 2026-04-30. All 9 classes confirmed present in `FruitNinja.exe`. None marked `NOT FOUND`.

These specs give the implementer just enough info to write empty-stub headers/source files for classes referenced by `// TODO:` comments in `src/`. Each stub should preserve the class name, base class, key public method signatures, and singleton/storage pattern from the binary so call sites compile and (eventually) link cleanly when the real implementation lands.

For richer field tables and Update behavior see linked existing docs:
- `docs/structs/ui-widgets.md` — ScoreControl, SpeedControl, CoinCounter, TimeControl
- `docs/structs/ui-controls2.md` — ComboControl
- `docs/structs/game-managers.md` — PowerUpManager, AchievementManager, NetworkManager, LeaderboardManager (manager coverage table)
- `docs/entities/bomb-flash.md` — BombFlash (full RE)
- `docs/systems/power-ups.md` — PowerUpManager method index
- `docs/functions/data-parsing.md` — AchievementManager::LoadAchievementInfo, PowerUpManager::Load

---

### ScoreControl

| Field | Value |
|-------|-------|
| Mangled symbol | `ScoreControl` (Ghidra-demangled), constructor at 0x00158c7c |
| Ctor address | 0x00158c7c (real), 0x00158d4c (alias), thunk wrapper 0x000f6bdc |
| Struct size | 0x100 bytes (confirmed via `operator_new(0x100)` in GameInit) |
| Base class | `HUDControl3d` (super up to 0x78) |
| Storage | Heap-owned by `HUD::AddControl(...)` in GameInit step 4. No direct `Game+0xNN` slot — HUD owns the pointer. |
| Purpose | Main score HUD: 16-digit display with per-digit alpha animation, sin-wobble pulse on score change, scale pulse driven by combo timer, new-highscore banner. |

**Methods (for stub):**
- `ScoreControl()` @ 0x00158c7c — Ctor: HUDControl3d base + load `score_alpha.tex`, init m_DigitAlpha[16], call `Reset()`.
- `~ScoreControl()` @ 0x00158394 / 0x00158418 / 0x00158494 — virtual dtors (regular/inplace/deleting).
- `Reset()` — called from end of ctor; clears digits/wobble. (Address inline in ctor; no separate exported symbol.)
- `Update(float dt)` @ 0x0015853c (335 lines) — core animation: digit alpha, wobble, scale interpolation, highscore detection.
- `LoadContent()` (vtable slot 2) — loads localised digit textures, called via vtable in GameInit after ctor.
- `Draw(_Matrix44 const&)` (vtable slot, ~0x158600+) — renders digits + multiplier + highscore banner. Inherits HUDControl3d::Draw scaffold.

Notes: Constructor in GameInit also sets size from `DAT_0016c9b8` (digit-cell scale) and position from `DisplayManager::GetWindowSize()`. These are caller-side, not part of the ctor body.

---

### ComboControl

| Field | Value |
|-------|-------|
| Mangled symbol | `ComboControl(int)` |
| Ctor address | 0x00136cc4 (real), 0x00136d1c (alias) |
| Struct size | 0x8C bytes (super 0x7C + lifetime + comboCount + char[8] label, max field at 0x84) |
| Base class | `HUDControl3d` |
| Storage | Pooled / fire-and-forget. Spawned by combo logic, self-destructs after 1.0s by setting `m_bPendingRemoval=1`. Owned by HUD until removed. |
| Purpose | Combo count pop-up (e.g. "x3"). 1-second lifetime, then self-removes. |

**Methods (for stub):**
- `ComboControl(int comboCount)` @ 0x00136cc4 — Ctor: HUDControl3d base, m_bNoDestructor=0, sets vtable, formats label via `OS_SPrintf(buf, 8, fmt, comboCount)`, lifetime=1.0f.
- `~ComboControl()` @ 0x00136c0c / 0x00136c4c / 0x00136c88 — virtual dtors.
- `Reset()` @ 0x00136bdc — empty body (no-op).
- `Update(float dt)` @ 0x00136be4 — `lifetime -= dt; if (lifetime < 0) m_bPendingRemoval = 1;`
- `Draw(...)` (vtable slot) — renders the formatted text label. (HUDControl3d Draw infrastructure.)

---

### SpeedControl

| Field | Value |
|-------|-------|
| Mangled symbol | `SpeedControl` |
| Ctor address | 0x0016133c (real), 0x00161444 (alias), thunk wrapper 0x000ffd38 |
| Struct size | 0xAC bytes (confirmed via `operator_new(0xac)` in `WaveManager::UpdateComboSpeed` at 0x00122ff6) |
| Base class | `HUDControl3d` |
| Storage | Owned by `WaveManager` — pointer at `WaveManager+0x00`. Created lazily inside `UpdateComboSpeed` when combo speed first becomes nonzero, destroyed by `DeleteSpeedControl`. NOT created by GameInit. |
| Purpose | Combo speed/blitz gauge: scale pulse + particle emitter spawned on combo increase, fade-in/out alpha, integrates with WaveManager combo bonus. |

**Methods (for stub):**
- `SpeedControl()` @ 0x0016133c — Ctor: HUDControl3d base, loads localised speed gauge texture, sizes from texture dims, all anim fields zeroed.
- `~SpeedControl()` @ 0x00161558 / 0x001615d4 / 0x00161650 — virtual dtors.
- `Update(float dt)` @ 0x00160dc4 (202 lines) — reads WaveManager combo, animates scale, spawns/clears PSPParticleEmitter on changes.
- `DeleteSpeedControl()` @ 0x001217d4 — already RE'd; standalone non-member function that calls `~SpeedControl` + `delete` on `WaveManager+0x00` and nulls it.
- `Draw(...)` (vtable slot) — gauge sprite + colour flash on combo change.
- `LoadContent()` (vtable slot 2) — texture reload (called via vtable[2] from ctor flow).

Notes: WaveManager creates SpeedControl with `Mortar::Delegate1<void,HUDControl*>::QCallee<WaveManager>` callback so HUD::Remove notifies WaveManager to null its pointer.

---

### CoinCounter

| Field | Value |
|-------|-------|
| Mangled symbol | `CoinCounter` |
| Ctor address | 0x00135600 (real), 0x00135644 (alias), thunk wrapper 0x000f43d4 |
| Struct size | 0xD4 bytes (confirmed via `operator_new(0xd4)` in GameInit; doc estimate 0x94 was a lower bound — actual allocation is 0xD4) |
| Base class | `HUDControl3d` |
| Storage | `Game+0x178` (owned via `*(CoinCounter**)(Game+0x178) = coinCtrl`). Created in GameInit step 5. |
| Purpose | Coin count display HUD. Update is a true no-op (returns immediately); all visual logic in Draw. Renders coin texture quad + text via font. |

**Methods (for stub):**
- `CoinCounter()` @ 0x00135600 — Ctor: HUDControl3d base, m_CoinCount=0, alpha fields init, vtable set.
- `~CoinCounter()` @ 0x0013558c / 0x001355b8 / 0x001355dc — virtual dtors.
- `Reset()` @ 0x00135548 — clamps `field_0x8C` to [0, 1.0], sets `field_0x90 = 1.0f`.
- `Update(float dt)` @ 0x00135580 — empty / immediate return.
- `Draw(...)` @ 0x0013569c — alpha-gate then draw coin texture quad + font string at offset (-15, 0).
- `LoadContent()` (vtable slot 2) — coin texture load. Called from GameInit via `vtable[2]` immediately after ctor.

Notes: There is no exported `CoinCounter::SetCoins` / `AddCoins`. Coin count `m_CoinCount` (offset 0x7C, ushort) appears to be written by external callers directly. `Coin::ClearCoins` (referenced at GameInit.cpp:407) is at 0x001731b8 — that's a `Coin::` static, NOT `CoinCounter::` — it removes Coin entities from the world, not a counter reset.

---

### BombFlash

| Field | Value |
|-------|-------|
| Mangled symbol | `BombFlash` |
| Ctor address | 0x00171a14 (real), 0x00171a50 (alias) |
| Struct size | 0x44 bytes (68 bytes), confirmed via `BombFlash::CreatePool(0x20)` → 32-element array |
| Base class | None (standalone struct with vtable). NOT an Entity. |
| Storage | Pooled. `BombFlash::CreatePool(0x20)` allocates a 32-flash static array. `MakeFlash` activates a free slot; `Update`/`Draw` iterate. |
| Purpose | White flash sprite spawned on bomb hit. Quadratic scale + alpha animation over a short lifetime, then deactivates and returns to pool. |

**Methods (for stub):**
- `BombFlash()` @ 0x00171a14 — Ctor: vtable, two Colour init, SmartPtr<Texture> init, m_bActive=0.
- `~BombFlash()` @ 0x00171f38 / 0x00171fb8 — virtual dtors.
- `BombFlash::CreatePool(int n)` @ 0x00170f84 — currently a stub in binary (returns param). Real allocation handled by static-array sizing in `MakeFlash`.
- `BombFlash::MakeFlash(Colour, Vec3* pos, Vec3* dir, SmartPtr<Texture>* tex, ...)` @ 0x001723f4 — activate a pooled flash with the given texture/colour/direction.
- `BombFlash::Update(float dt)` @ 0x00171038 (61 lines) — quadratic scale + alpha animation; deactivates when lifetime expires.
- `BombFlash::UpdateActiveFlashes(float dt)` @ 0x00171028 — static; iterates pool calling Update.
- `BombFlash::DrawActiveFlashes()` @ 0x0017102c — static; iterates pool calling Draw.
- `BombFlash::RemoveAllFlashes()` @ 0x00170fe4 — static; deactivates every slot (called on game reset).
- `BombFlash::CleanUp()` @ 0x00171f64 — static; reverse-iterates pool, destructs each, frees backing memory.

See `docs/entities/bomb-flash.md` for full RE. Note `BombFlashFull` @ 0x00168f24 is a separate variant referenced by Bomb code.

---

### LeaderboardManager

| Field | Value |
|-------|-------|
| Mangled symbol | `LeaderboardManager` |
| Ctor address | 0x001113a8 (real), 0x001113c0 (alias), thunk wrapper 0x00101a00 |
| Struct size | 0x40 bytes / 64 bytes (constructor zeroes 4 ulongs in a loop until `this+1` boundary; documented size in `game-managers.md`) |
| Base class | None |
| Storage | **GetInstance() singleton** @ `LeaderboardManager::GetInstance()` 0x001114b8. Lazy-init via `__cxa_guard_acquire`, registered with `__aeabi_atexit`. |
| Purpose | Online leaderboard handler (OpenFeint / GameCenter). Defunct online service — for the port this can be a no-op stub that satisfies callers. |

**Methods (for stub):**
- `LeaderboardManager()` @ 0x001113a8 — Ctor: zero-fills the 0x40-byte struct.
- `~LeaderboardManager()` @ 0x001113d8 / 0x001113dc — empty destructors.
- `LeaderboardManager::GetInstance()` @ 0x001114b8 — lazy singleton accessor.
- `RefreshLeaderboard(...)` @ 0x00111664 — refresh requested data (network call in original; no-op for port).
- `UpdateLeaderboard(...)` @ 0x0013afbc — push score (network call in original).

Notes: Real internal score-pushing also goes through `NetworkManager` and platform-specific `OpenFeint`/`GameCenter` shims. For the port, this class is **skipped** per `docs/structs/game-managers.md` — empty stub with a no-op `GetInstance()` returning a static instance is sufficient.

---

### PowerUpManager

| Field | Value |
|-------|-------|
| Mangled symbol | `PowerUpManager` |
| Ctor address | 0x00117d20 (real), 0x00117d60 (alias), thunk wrapper 0x00104004 |
| Struct size | ~0x90 / 144 bytes (per `docs/structs/game-managers.md`; ctor inits `m_field70`/`m_field74` at 0x70-0x74). Last accessed offset 0x84. |
| Base class | None |
| Storage | **GetInstance() singleton** @ `PowerUpManager::GetInstance()` 0x00118134. Lazy-init via `__cxa_guard_acquire`. |
| Purpose | Tracks active power-ups: hash→PowerUp* maps, active list, screen-effect map, dt multiplier (slow-time), score gain/loss multipliers. Drives blitz/chrono/double-points/etc. modifiers. |

**Methods (for stub):**
- `PowerUpManager()` @ 0x00117d20 — Ctor: 3 std::map ctors, 2 std::list ctors, init m_DtMod/m_field70 = 1.0f.
- `~PowerUpManager()` @ 0x001187fc / 0x00118880 — virtual dtors.
- `PowerUpManager::GetInstance()` @ 0x00118134 — lazy singleton accessor.
- `PowerUpManager::Update(float dt)` @ 0x001189b4 (110 lines) — tick all active powers, handle expiry. Wrapper symbol `PowerUpManager_Update` @ 0x000f3ccc / 0x001189b4.
- `PowerUpManager::Reset(bool fullReset)` @ 0x00119b08 — clears all active powers + state.
- `PowerUpManager::ClearTimedPowers()` @ 0x00118904 — remove timed-only powers (called on bomb hit; survives non-timed unlocks).
- `PowerUpManager::ActivatePower(uint32 hash)` @ 0x001197c4 (118 lines) — clone PowerUp by hash and activate.
- `PowerUpManager::Load()` @ 0x00119cb0 — load `poweruplist.xml` into hash maps. Called once at boot.
- `PowerUpManager::ApplyDtMod(float)` @ 0x001204dc — m_DtMod *= param (slow-time hook).
- `PowerUpManager::SlowClock(...)` @ 0x001204cc — slow-time activation.
- `PowerUpManager::ClearScreenEffects()` @ 0x00117ed8 — clear all screen-effect entries.
- `PowerUpManager::GetScoreGainMultiplier()` @ 0x0010ad34 — returns m_ScoreGainMult * m_ScoreGainFactor.
- `PowerUpManager::GetScoreLossMultiplier()` @ 0x0010ad40 — returns +0x80 * +0x84.

Notes: Per `docs/systems/power-ups.md`, this class is REQUIRED for full fidelity (drives many gameplay modifiers). A no-op stub will compile but will silently disable blitz mode, freeze, double points, etc. Mark all the no-op methods in the stub with `// TODO: real impl pending — see docs/systems/power-ups.md`.

---

### AchievementManager

| Field | Value |
|-------|-------|
| Mangled symbol | `AchievementManager` |
| Ctor address | 0x00108930 (real), 0x00108954 (alias), thunk wrapper 0x001059c0 |
| Struct size | Per docs: "1 byte stub struct" — ctor allocates 11 contiguous std::map members, no scalar fields. Effective size dominated by 11 × map header (~24 bytes each = ~264 bytes). |
| Base class | None |
| Storage | **GetInstance() singleton** @ `AchievementManager::GetInstance()` 0x00108f64. Lazy-init via `__cxa_guard_acquire`. |
| Purpose | Achievement tracking. Holds `map<hash, AchievementInfo*>` plus 10 type-categorised sub-maps. Offline tracking + network unlock submission. |

**Methods (for stub):**
- `AchievementManager()` @ 0x00108930 — Ctor: constructs 11 std::map members in a loop (descending counter from 9 to -2 = 11 maps total).
- `~AchievementManager()` @ 0x00109028 / 0x00109078 — virtual dtors.
- `AchievementManager::GetInstance()` @ 0x00108f64 — lazy singleton accessor.
- `AchievementManager::LoadAchievementInfo()` @ 0x00109200 (279 lines) — parse `achievementlist.xml` into 11 type categories, 0x1A0 bytes per AchievementInfo entry.
- `AchievementManager::UnLoadAchievementInfo()` @ 0x00108fb4 — destroy maps, free entries.
- `AchievementManager::UnlockAchievement(...)` @ 0x0018d690 (cross-binary) / `UnlockAchievementInNetwork` @ 0x001085a0 — queue achievement unlock.
- `AchievementManager::UnlockAchievements(...)` @ 0x0010e12c — bulk variant.
- `AchievementManager::UnlockTotalFruitAchievement(...)` @ 0x00108eec — specialised total-fruit hook.
- `AchievementManager::UnlockConsecutiveAchievement(...)` @ 0x00108c40 — specialised consecutive-slice hook.

Notes: For port fidelity, `LoadAchievementInfo` should be ported (achievement metadata for in-game UI), but actual unlock submission to OpenFeint/GameCenter is dead code. A no-op stub for unlocks is fine; preserve `LoadAchievementInfo` scaffolding for whenever the implementer reaches it.

---

### NetworkManager

| Field | Value |
|-------|-------|
| Mangled symbol | `Mortar::NetworkManager` (in `Mortar` namespace) |
| Ctor address | 0x0018e05c (real), 0x0018e25c (alias), thunk wrapper 0x00100518 |
| Struct size | 668 bytes (per `docs/structs/game-managers.md`). Ctor explicitly init fields up to 0x29B. Includes 9 typed Delegates, a std::map, 3 BUTTON_INFO sub-structs. |
| Base class | None |
| Storage | **GetInstance() singleton** @ `Mortar::NetworkManager::GetInstance()` 0x0018e210. Lazy-init via `__cxa_guard_acquire`. Singleton stored at very high BSS offset (`+0x3e0` past the guard byte). |
| Purpose | OpenFeint + GameCenter + P2P multiplayer manager. Manages popup-alert button registration, status messages, network event delegates. **Skipped for port** per project policy (online services). |

**Methods (for stub):**
- `Mortar::NetworkManager::NetworkManager()` @ 0x0018e05c — Ctor: 9 Delegate ctors + std::map + 3 BUTTON_INFO + flag init + 3 MakeDelegate_Engine_* calls + DeregisterAllPopupAlertButtons + SetStatusMessageTextDefaults.
- `~NetworkManager()` @ 0x0018da94 / 0x0018dba4 — virtual dtors: destroys 3 BUTTON_INFOs (reverse), map, 9 Delegates.
- `Mortar::NetworkManager::GetInstance()` @ 0x0018e210 — lazy singleton accessor.
- `DeregisterAllPopupAlertButtons(this)` — internal helper (from ctor body).
- `SetStatusMessageTextDefaults(this)` — internal helper (from ctor body).
- `SetP2PMessageHandlerCallback(...)` — symbol exists at 0x000f3714 thunk; member of NetworkManager.
- `IsOnlineMultiplayer(...)` — symbol exists in list_methods; member of NetworkManager.
- `P2PConnect(...)` — symbol exists in list_methods; member of NetworkManager.
- `ChangePreferredNetworkProvider(long)` — class symbol in `list_classes`; member.
- `AreGameCenterConnectionAttemptsAllowed(...)` — class symbol in `list_classes`; member.

Notes: Port should provide an empty `NetworkManager` class with no-op `GetInstance()` returning a static instance, and no-op stubs for any method the implementer's TODOs reference. Do **not** port real network code — the project policy is to skip online services. Confirm the `Mortar::` namespace placement when writing the header.

---

## Implementer hints

For each stub:

1. Place header at `src/<area>/<ClassName>.h` per existing layout (e.g. ScoreControl/SpeedControl/CoinCounter/ComboControl under `src/hud/`, BombFlash under `src/entities/`, the four managers under `src/game/managers/` or alongside other singletons). Follow the convention already in use for `WaveManager`/`HUD`/etc.
2. Inherit from `HUDControl3d` for the 5 HUD widgets (ScoreControl, ComboControl, SpeedControl, CoinCounter — BombFlash is standalone).
3. Match the binary's vtable shape: virtual `Init()` / `LoadContent()` / `Update(float)` / `Draw(...)` slots for HUDControl3d descendants, plus virtual destructor.
4. For singletons (PowerUpManager, AchievementManager, NetworkManager, LeaderboardManager): provide `static T& GetInstance()` returning a function-local static. Match the original lazy-init guard semantics conceptually but use the C++11 thread-safe local static.
5. CoinCounter goes at `Game+0x178` (existing `Game.h:96` slot). BombFlash pool already exists (see `docs/entities/bomb-flash.md`). SpeedControl goes at `WaveManager+0x00`, lifecycle managed by `UpdateComboSpeed` + `DeleteSpeedControl`.
6. For all manager methods called from existing TODOs, provide an empty body that prints nothing (or, optionally, a one-time `Log_Stub("PowerUpManager::ClearTimedPowers")` style trace). Do NOT add functional placeholders that "kind of" work — the goal is to compile + link, not to fake gameplay. The TODO comments already in `src/` mark each call site.
7. Constructor in GameInit uses `operator new(SIZE)` — but for the port, prefer plain `new ScoreControl()` etc. The size is informational (and a useful spec sanity-check when comparing sizeof to the binary).
