# Gameplay Implementation Handover

**Branch**: `screen-fidelity-fixes` (218ca6d, 2026-04-29)  
**Surveyed**: 2026-04-29T03:30Z  
**Scope**: End-to-end gameplay path from mode selection through game-over

---

## Status Overview

The FruitNinja port has **functional screen navigation** (MainScreen → GameModeScreen → gameplay) but **gameplay itself is heavily stubbed**. Wave spawning completely stubbed (WaveManager::Update no-op), scoring/miss tracking absent, game-over detection missing, mode-specific gates absent.

**Critical path structurally works but produces zero gameplay**: fruit never spawn, misses never counted, bombs trigger shake but no consequences, no TimeControl countdown or state transition.

---

## Per-Section Checklist

### A. Mode-Pick → Game-Start Transition

**🔶 GameModeScreen Mode Callbacks** | `src/screens/GameModeScreen.cpp:100-102`  
- ✅ Callbacks exist, wired to MenuButton
- ❌ **BUG**: Missing `game->gameMode = 0/1/2` assignment in callbacks
- **Fix**: Add gameMode assignment (0=Classic, 1=Arcade, 2=Zen)

**🔶 GameModeScreen Fade-Out**  
- ✅ State machine decay exists
- ❌ **BUG**: No call to `mainScreen->SetState(STATE_GAME_START)` when fade completes
- **Fix**: Add state transition when m_SecondaryAlpha < SECONDARY_CLAMP

**✅ MainScreen STATE_GAME_START** | `src/screens/MainScreen.cpp:234-268`  
- ✅ Calls WaveManager::GetInstance()->Reset(true)
- ❌ **BLOCKER**: WaveManager::Reset() is no-op—no spawn data loaded

**❌ WaveManager::Reset()** | `src/game/WaveManager.cpp:39`  
- **Completely stubbed**
- **BLOCKER FOR GAMEPLAY LAUNCH**

---

### B. WaveManager Runtime

**❌ WaveManager::Update()** | `src/game/WaveManager.cpp:49`  
- **No-op stub**
- **CRITICAL BLOCKER**: Without it, no spawners tick

**❌ WaveManager::UpdateWave() / SpawnFruit() / SpawnBomb()**  
- All stubbed
- **BLOCKER**: ActorManager::Add() never called

**❌ Wave XML Load**  
- WaveManager::Init() stubbed
- **BLOCKER**: No wave data available

---

### C. HUD Widgets During Gameplay

**❌ ScoreControl / TimeControl / ComboControl / SpeedControl**  
- **Zero classes defined**
- **Need**: Create stubs, instantiate on STATE_GAME_START

**✅ MissControl**  
- ✅ Class exists, pool allocated
- ❌ Fruit crit paths never call it

**🔶 HUD Add/Remove**  
- ✅ HUD list wired
- ❌ **Gap**: No code manages gameplay HUD lifecycle

---

### D. Score / Miss / Bomb Tracking

**❌ Fruit::OnSlice → Score Increment** | `src/entities/Fruit.cpp:641`  
- ✅ CollisionResponse exists
- ❌ **No score increment**
- **Fix**: Add FruitInfo[type].m_Score * (crit ? 2 : 1)

**❌ Fruit::OnFallBelow → Miss Penalty** | `src/entities/Fruit.cpp:481`  
- ✅ Offscreen detection exists
- ❌ **KillFruit stub**: miss penalty TODO
- **Fix**: Implement miss counting, check >= 3

**❌ Bomb-Hit → Game-Over** | `src/entities/Bomb.cpp:643`  
- ❌ **Game-over gate missing**
- **Fix**: Route bomb hit to game-over handler

---

### E. Game-Over Flow

**❌ STATE_GAME_OVER Handler**  
- Enum value missing
- No state handler
- **Need**: Define state, add handler

**❌ TimeControl Class**  
- Not implemented
- **BLOCKER for Arcade/Zen**

**❌ GameOverScreen**  
- Stub only
- Need: HUDControl3d subclass

---

### F. Pause Flow

**⏭️ Skip for MVP** — incomplete wiring

---

### G. Mode-Specific Behaviour

**❌ Classic: Miss Limit 3, No Timer**  
- ❌ Miss limit gate stubbed
- **Fix**: Implement miss counting

**❌ Arcade: 60s Timer, Power-ups**  
- ❌ TimeControl not created
- **Fix**: Create TimeControl(60)

**❌ Zen: 90s Timer, Bombs OFF**  
- ❌ TimeControl not created
- ❌ Bomb spawn gate missing
- **Fix**: Create TimeControl(90), gate bombs

---

### H. Power-Ups (Arcade-Specific)

**❌ PowerUpManager / Modifiers** | Completely missing  
- **Deferred**: Post-MVP

---

### I. End-Game Stats

**❌ BonusScreen / ScoreMultiplierBoard**  
- **Deferred**: Stats/cosmetic features

---

## Implementation Order (Phased Approach)

### Phase 1: Foundation (Unblock Spawning)

1. WaveManager::Init() + XML Loading [CRITICAL] (1-2 days)
2. WaveManager::Reset(bool) (0.5 days)
3. WaveManager::UpdateWave() + Spawner Tick (1 day)
4. SpawnFruit() + SpawnBomb() (1 day)

### Phase 2: Scoring + Miss Logic

5. Fruit::Slice() Score Increment (0.5 days)
6. Fruit::KillFruit() Miss Penalty (1 day)
7. Bomb::CollisionResponse Game-Over Gate (0.5 days)

### Phase 3: Mode-Specific Game-Over

8. Create TimeControl Class (1 day)
9. Wire TimeControl on Game Start (0.5 days)

### Phase 4: Game-Over UI

10. Implement GameOverScreen (2 days)
11. Add MainScreen STATE_GAME_OVER Handler (1 day)
12. Fix GameModeScreen Callbacks [CRITICAL BUG FIX] (0.5 days)

---

## Risk Areas

1. WaveManager frame pacing (fixed vs variable timestep)
2. Critical-hit defaults (unconfirmed from binary)
3. Mode-specific wave progression (undefined in port)
4. Game-over timing gates (binary pauseFlag dependency)
5. Combo frame-sync (timing-sensitive)
6. Power-up modifier order (once implemented)

---

## File Index

| Section | File | Functions |
|---------|------|-----------|
| A | src/screens/GameModeScreen.cpp | ClassicModeCallback, ZenModeCallback, ArcadeModeCallback, Update |
| A | src/screens/MainScreen.cpp | STATE_GAME_START case (234) |
| B | src/game/WaveManager.cpp | Init, Reset, Update, UpdateWave, SpawnFruit, SpawnBomb |
| D | src/entities/Fruit.cpp | CollisionResponse, Slice, KillFruit |
| D | src/entities/Bomb.cpp | CollisionResponse |
| E | src/screens/MainScreen.cpp | STATE_GAME_OVER (to be added) |
| G | src/game/GameInit.cpp | GameUpdate loop (98) |
| H | src/hud/TimeControl.h | (to be created) |
| I | src/screens/GameOverScreen.h | (to be expanded) |

