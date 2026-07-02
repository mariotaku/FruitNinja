# Online Services Stub Audit

<!-- Analysed: 2026-06-13 — FruitNinja_v1_6_1.exe comprehensive stub inventory -->

This audit confirms that all ~20 defunct online-service stubs (GameSpy / P2P /
GameCenter / OpenFeint / online news / online leaderboards / DRM / profanity
filter) in the port have no **live calls that block gameplay**. Every reference
in `src/` either routes to an existing `// Defunct:` no-op stub or returns a
safe default, and all offline/local paths remain intact.

Per `CLAUDE.md`: "Defunct features are **stubbed**, never skipped." This audit
inventories the ~20 network/online stubs in the current port (v1.6.1), confirms
each has a safe default return or no-op body, and lists the live call sites to
verify they are safe.

## 1. Per-Subsystem Inventory

All stubs are in `src/` and carry source-side `// Defunct:` markers except where noted.
Binary addresses are for FruitNinja_v1_6_1.exe v1.6.1 (0x10000 image base).

| Subsystem / class | Status | Methods live code calls | Safe return | Binary addr |
|---|---|---|---|---|
| **Mortar::NetworkManager** | ALREADY-STUBBED-OK | GetInstance, SpawnThreadController, UpdateNews, CancelNewsDisplay, ConnectGameCenter, PublishText, SetLeaderboardScore, LaunchDashboard | this / void / false | ctor 0x231c40, GetInstance 0x231e7c |
| **GetSocialNetworkProvider** (free fn) | ALREADY-STUBBED-OK | FruitFactControl::UpdateLeaderboard | 0 (OpenFeint) | GOT flag |
| **Mortar::NetworkPacket** (base) | ALREADY-STUBBED-OK | m_typeId read by PacketFactory; base of PointsPacket, FruitSlicedPacket, StartGamePacket, WaveSyncPacket, PlayerDisconnectGamePacket | n/a (data) | ctor 0x00102c3c |
| **PacketFactory** | ALREADY-STUBBED-OK | Create(id) called by P2P message handlers | nullptr for unknown id | 0x157b20 |
| **P2PMessageHandling** (free fns) | ALREADY-STUBBED-OK | P2PConnect, DisconnectP2P, IsP2POnline, SendP2PPacket, GlobalP2PMessageHandler | false / void | SendP2PPacket 0x157630 EMPTY, GlobalP2PMessageHandler 0x15761c EMPTY |
| **ProfanityFilter** | EMPTY-TU-OK | none | — | only _GLOBAL__I_ at 0x146f70 |
| **DRM: Licensing::IsLicensed** | ALREADY-STUBBED-OK | legacy SKU checks (unused in port) | true (hardcoded) | 0x1ca830 |
| **DRMManager** | EMPTY-TU-OK | none | — | only _GLOBAL__I_ at 0x1348d8 |
| **LeaderboardManager** | ALREADY-STUBBED-OK | GetInstance, RefreshLeaderboard, GetLeaderboard, ClearScores | nullptr / void | ctor 0x1113a8, GetInstance 0x1114b8 |
| **Mortar::OpenFeintNewsRenderer** (engine base) | ALREADY-STUBBED-OK | StartNewsRender, CancelNewsRender, Update, Draw (virtuals) | void | ctor 0x191a94 |
| **FruitNinjaNewsControl** (: OpenFeintNewsRenderer) | ALREADY-STUBBED-OK | IsDisplayingNews, OnNewsFinished/CancelNews | false / void | ctor 0x1a13d0 |
| **FriendLeaderboardItem** (: LeaderboardItem) | ALREADY-STUBBED-OK | ctor, CollideWithButton | false | ctor 0x13d210 |
| **GameSpyScreen** | EMPTY-TU-OK | none | — | only _GLOBAL__I_ at 0x1891e4 |
| **AchievementManager** | LIVE-LOAD-BEARING | LoadAchievementInfo (GameInitialise step 13), AchievementExists (ItemManager init-time) | populates m_All keyed by StringHash(id); gates shop item lock state | ctor 0x00108930, LoadAchievementInfo 0x00118198, AchievementExists 0x00108ea4 |
| **FruitFactLeaderboard::Init** | STUBBED-COSMETIC | calls stubbed LeaderboardManager | empty board | ctor 0x176980 |

**STUBBED-COSMETIC note:** FruitFactLeaderboard::Init carries a TODO comment
stating it is "BLOCKED on FNHighscore + LeaderboardManager (defunct)". The
TODO over-states: LeaderboardManager is already stubbed (GetLeaderboard returns
nullptr). The Init body can be a no-op that renders an empty leaderboard.

## 2. Live Call Sites (All Safe)

All references in `src/` are to either:
- A class with a `// Defunct:` method body returning a safe default or void.
- An empty translation-unit that emits only `_GLOBAL__I_` static init.
- A comment-only reference (not a call).

The offline/local path is unbroken because IsOnline / UserHasEnabledNetwork /
HasUnreadNews / IsP2POnline all return their offline defaults (false / 0 / nullptr).

### Live call sites

| Caller | File | What it calls | Classification |
|---|---|---|---|
| `DojoScreen::Update` | `src/screens/DojoScreen.cpp:325-327` | NetworkManager::SpawnThreadController (state-4 dashboard) | **dead** — state never reaches 4; transitions to 0 |
| `MainScreen::Update` | `src/screens/MainScreen.cpp:358-374` | NewsControl methods (STATE_NEWS) | **dead** — states transition back without invoking methods |
| `GameOverScreen::Update` | `src/screens/GameOverScreen.cpp` | FacebookShare / LeaderboardSubmit via NetworkManager | **dead** — routed to no-op stubs |
| `PauseScreen::QuitToMenu` | `src/screens/PauseScreen.cpp:112` | NetworkManager reference | **comment** — text reference only |
| `BonusScreen::Update` | `src/screens/BonusScreen.cpp` | LeaderboardManager::RefreshLeaderboard | **dead** — routed to nullptr return |
| `FruitFactControl::Update` | `src/screens/FruitFactControl.cpp` | GetSocialNetworkProvider + LeaderboardManager | **dead** — routed to no-op stubs |
| `ItemManager::LoadItemData` | `src/game/ItemManager.cpp:121-122` | AchievementManager::AchievementExists (init-time) | **live** — load-bearing: m_All must be populated by LoadAchievementInfo (called before LoadItemData in GameInitialise step 13); gates whether cost>0 items are locked |
| `GameInitialise` / `FruitSaveData` | various | AchievementManager / LeaderboardManager / NetworkManager | **comments** — text references, not calls |

## 3. Verdict

**All ~20 network/online stubs are safe.** Every live reference either:
- Routes to a no-op method body returning false / nullptr / 0.
- Routes to an empty translation-unit (ProfanityFilter, DRMManager, GameSpyScreen).
- Is a comment-only reference.
- Is init-time, not gameplay-loop.

No call site is blocked. No offline path is broken. Nothing prevents the game
from running.
