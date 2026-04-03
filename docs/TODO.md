# Reverse Engineering TODO

## Next Steps (prioritized)

### 1. Wave Selection & Dispatch — DONE
- [x] `WaveManager::GetNextWave` (0x124f10, 227 lines) — score-based wave selection, ChooseFrom, weighted random
- [x] `WaveManager::UpdateWave` (0x125390, 298 lines) — spawner processing, SpawnFruit/SpawnBomb dispatch, power-up fruit
- [x] `WaveManager::SetupWaveQue` (0x124564, 142 lines) — budget-based queue fill (27 time units), randomise, bookend waves
- See `docs/functions/wave.md`

### 2. Fruit::RandomFruit — DONE
- [x] `Fruit::RandomFruit` (0x176564, 113 lines) — weighted fruit selection
- 4 paths: normal/critical × includeOnSide/available-only
- Cumulative weight tables lazy-initialized from FRUIT_INFO[].chance
- hitInfluence filter adds variety by excluding recently-sliced fruits
- See `docs/functions/fruit.md`

### 3. ActorManager — DONE
- [x] `ActorManager::Add` (0x17068c) — free pool search + factory fallback, entity recycling
- [x] `ActorManager::Update` (0x1701f4) — tick all entities, deactivation queue
- [x] `ActorManager::Draw` (0x16fe7c) — render all active entities
- [x] `ActorManager::Deactivate` (0x170184) — move to free pool
- [x] `ActorManager::Remove` (0x1702d8) — destroy + erase
- [x] Struct layout (~0x106C), entity flags, vtable offsets
- See `docs/functions/actor-manager.md`

### 4. Data Parsing (XML loaders) — DONE
- [x] `PowerUpManager::Load` (0x119cb0, 80 lines) — poweruplist.xml → map\<hash, PowerUp*\> + effects
- [x] `BonusManager::Init` (0x10e8fc, 49 lines) — bonusawards.xml → vector\<BonusType\> + display order
- [x] `ItemManager::LoadItemData` (0x113200, 190 lines) — itemlist.xml → items + per-type maps + save loading
- [x] `AchievementManager::LoadAchievementInfo` (0x109200, 279 lines) — achievementlist.xml → 11 type categories
- [x] `PSPParticleManager::LoadFile` (0x115f60, 722 lines) — particles XML → 1024-particle pool + templates
- See `docs/functions/data-parsing.md`

### 5. Sound System (needed for port) — MOSTLY DONE
- [x] `BadaSound::SFXLoad` (0x18b1f4, 72 lines) — .wav.pcm → hash table + SoundEffectBada (256 max)
- [x] `BadaSound::SFXPlay` (0x18b130, 20 lines) — 8-slot concurrent playback, find free + prepare
- [x] `BadaSound::MusicPlay/Stop/Pause/Resume/Mute/SetVolume` — all decompiled, uses Osp::Media::Player
- [x] `BadaSound struct` — full layout: hash table (256), effects (256), 8 active slots, volume floats
- [ ] `Mortar::MortarSound` / `MortarSoundMAM` (15 funcs) — sound handle API (behind GOT thunks, hard to resolve)
- See `docs/systems/sound-system.md`

### 6. Remaining Gameplay Functions
- [x] `Fruit::SetFruitType` (0x17621c, 46 lines) — set type, scale, collision sphere from FRUIT_INFO
- [x] `Fruit::EnableCollision` (0x176354, 36 lines) — toggle ColSphere on/off
- [x] `TimeModifier::ParseSpecific` (0x120100) + `UpdateSpecific` (0x1200a0) — stop/slow/ramp clock, addTime
- [x] `SlashModifier::ParseSpecific` (0x11f464) — blade colours, width, texture (no UpdateSpecific, apply-only)
- [x] `WaveModifier::ParseSpecific` (0x12836c) + `UpdateSpecific` (0x1280e4) — fruit/bomb multipliers, overrides
- [x] `ScoreModifier::ParseSpecific` (0x11ccb0) + `UpdateSpecific` (0x11cc50) — gain/loss add+multiply
- [x] `SlashEntity::InitPoints` (0x17c340, ~40 lines) — 2 vertex buffers × (splitPoint+2) × 0x24 bytes
- [x] `Fruit::DrawShadows` (0x178f28, 33 lines) — batched shadow tri-strip via ActorManager iteration
- [x] `DrawStartFade` (0x16ab10, ~45 lines) — loading fade overlay with alpha+brightness ramp
- [x] `MainScreen::DrawPostEffects` (0x14ac94) — **no-op stub** (bx lr), skip for port
- [x] `MainScreen::UpdateScreenElements` (0x14ad3c, 55 lines) — logo bounce physics
- See `docs/functions/fruit.md`

---

## Unfinished Analysis

### Power-Up System — DONE

See `docs/systems/power-ups.md` and `docs/functions/power-ups.md`. Full system recovered:
- PowerUp struct (~0xB8 bytes): name, hash, modifiers list, textures, screen effect
- 4 modifier types fully decompiled: ScoreModifier, TimeModifier, SlashModifier, WaveModifier
- ActivatePower + Activate flow fully traced
- PowerUpManager::Update (0x118a00, 110 lines) decompiled — ticks active powers, handles expiry
- PowerUp::Update (0x117f90) + Deactivate (0x117f18) decompiled
- All Parse/UpdateSpecific/ApplyModifier/RemoveModifier methods mapped

**Remaining** (low priority):
- `ScreenEffect::Activate/Parse` — visual effect details
- `PROBABILITY_OVERIDE::Parse` — wave override entry parsing

### Touch → Slash Input Pipeline — DONE

See `docs/systems/touch-input.md`. Full pipeline recovered:
- GlesForm::TransformTouchPos: raw portrait → game landscape coords (480×320, axes swapped)
- Touch::__UpdateInternal: ring buffer with TEvnt structs
- SlashEntity::TouchDown/MoveX/MoveY: maps input to entity position
- Multi-touch: 8 simultaneous touches, unique IDs

### Particle System — DONE

See `docs/systems/particles.md`. Full architecture recovered:
- PSPParticleManager: pool-based emitter management, template search by hash
- PSPParticleEmitter: 0x4C bytes, linked list, position/velocity/scale/timeScale
- PSPEmitterTemplate: loaded from data file, contains ParticleSets with spawn timing
- PSPParticle: 0xA4 bytes per particle
- AddEmitter, Update, Draw all decompiled
- 3 draw layers: -1 (background), 0 (mid), 1 (foreground)

**Still undecompiled:** AddParticle full (313 lines, only first 80 read), LoadFile, Draw full (382 lines)

### SplatEntity System — DONE

See `docs/systems/effects.md`. DrawActiveSplats fully decompiled:
- Batched triangle list rendering with QUADCUSTOMVERTEX
- 6 splat variants, pool-based

### BombFlash / BombBlast — DONE

See `docs/systems/effects.md`. Both Update functions fully decompiled:
- BombFlash: quadratic scale + alpha fade animation (61 lines)
- BombBlast: expanding radius, dual velocity, 3s lifetime (32 lines)

### Coin System (low priority, 16 functions)

- `Coin::MakeCoins` — seen in scoring pipeline
- `Coin::ClearCoins` — seen in GameExit
- Simple bouncing coin entity for visual reward feedback

## Graphics / Rendering — Not Yet Analyzed

### High Priority

- [x] `Mortar::Model::Draw` (0x1930e0) — depth-sorted multi-mesh rendering. See `docs/systems/rendering-detail.md`
- [x] `Mortar::Font::Load` (0x199e9c) — BMFont .fnt parser, 270 lines. DrawString = 13 params, ~300 lines
- [x] `TintWhite` / `TintColour` — float RGB → packed BGRA byte conversion + channel multiply
- [x] `SetupQuad` / `AddQuad` — quad builders with clipping. QUADCUSTOMVERTEX = 36 bytes confirmed

### Medium Priority

- [x] `SplatEffect` — textured quad overlay, no-op Update, Scale+Translate+DrawQuad in Draw
- [x] `Mortar::DisplayManager` — GL state singleton: viewport, clear/draw colour, light direction, depth/blend flags
- [x] `ScoreMultiplyerBoard` — arcade mode x2 popup, formats score text via Font::DrawString
- [x] `PowerUpShop` — simple HUDControl3d with vector<PowerUp*> and vector<Vec3>
- [x] `ShopListItem` (~0x284) — extends ScrollingMenuItem with item texture and selection flags

### Low Priority (engine internals)

- [ ] Effect/Shader system (186 funcs) — EffectGroup, EffectProperty, Effect_Bada::Pass. ES 1.x fixed pipeline — for port, replace with ES 2.0 shaders. Not worth deep RE.
- [x] MatrixManager / MatrixStack — known from T.NNN renames: Reset, Translate, Scale, SetCurrent, Upload. Offset +0x1094 in MatrixManager = the active MatrixStack.
- [x] GeometryBinding — PassBinding::Apply fully decompiled (0x1a39f8, 101 lines). Sets up glVertexPointer/NormalPointer/ColorPointer/TexCoordPointer.

## Sound System — Mostly Analyzed (147 funcs, 25+ done)

Documented: GameSound::SFXPlay, MAMAudioThread struct, architecture diagram, BadaSound full struct + all functions (see `docs/systems/sound-system.md`).

### High Priority (needed for port)

- [x] `Mortar::SoundManager` — SFXPlay/SFXPlayInternal/SetVolume decompiled. See `functions/sound.md`
- [x] `BadaSound::SFXLoad` (0x18b1f4, 72 lines) — full .wav.pcm loading pipeline decompiled
- [x] `BadaSound::SFXPlay` (0x18b130, 20 lines) — 8-slot concurrent playback
- [x] `BadaSound::MusicPlay/Stop/Pause/Resume/Mute/SetVolume` — all decompiled
- [x] `BadaSound` struct layout — hash table (256), effects (256), 8 active slots, SFX/music volume
- [ ] `Mortar::MortarSound` / `MortarSoundMAM` (15 funcs) — sound handle API (all behind GOT thunks)

### Medium Priority

- [ ] `MAMAudioController` (18 funcs) — Init, StartAudioSubsystem, StopAudioSubsystem. Bridges SoundManager to MAMAudioThread.
- [ ] `MAMAudioThread` (22 funcs) — 16kHz mixing thread with NLFQueue command buffers. Struct partially documented but Update/ThreadMainLoop not decompiled.
- [ ] `MortarAudioMixerBada` (17 funcs) — Bada-specific PCM mixing. Platform layer to replace with SDL2 audio callback.

### Low Priority

- [ ] `SoundEffect / SoundEffectBada` (61 funcs) — sound effect wrapper. May be an alternate path or unused in this build (large function count but not referenced from GameSound).

### For porting

The sound system has 4 layers:
```
GameSound (pool, 32 slots)       ← port this
  → Mortar::SoundManager           ← port this  
    → BadaSound / MortarAudioMixerBada  ← replace with SDL2 audio
      → MAMAudioThread (16kHz, NLFQueue)   ← replace with SDL2 audio callback
```

Only the top 2 layers (GameSound + SoundManager) need porting. The bottom 2 (BadaSound + MAMAudioThread) are replaced entirely by SDL2 raw audio.

---

## Recovered but not fully documented

### Math::Random (RNG) — DONE

64-bit LCG with Knuth MMIX multiplier. Full algorithm, constants, and port-ready C code in `docs/systems/rng.md`.
- Rand32: 64-bit LCG step, upper-32-bit output, multiply-high range reduction
- RandF: Rand32(0x7FFFF) / 524287.0 * max
- Seed: 0xDEADBEEF, Multiplier: 0x5D588B656C078965, Increment: 0x269EC3

### StringHash Algorithm — DONE

Full C implementation saved to `docs/systems/string-hash.md`. Jenkins lookup3 with case-folding, initial `c = 0x805 + len`.

### Math::SinIdx / CosIdx

16-bit fixed-point angle lookup. Scale factor: `angle_u16 = degrees * 0xb6`.
Can be replaced with standard `sin(angle * 2π / 65536)` and `cos()`.

## Intentionally Skipped

### Network / Social Features — NOT PORTING

The following classes are present in the binary but **intentionally excluded** from the port. They depend on defunct online services and Bada-specific APIs.

**Mortar::NetworkManager** (~40 functions):
- P2P multiplayer session management
- `SpawnThreadController`, `IsAnyPeerReadyForMultiplayer`
- `DownloadUserDataFromLeaderboard`
- `DrawNews`, `CancelNewsDisplay`
- Handles `FruitSlicedPacket`, `PointsPacket` sync for online multiplayer

**OpenFeint integration** (~20 functions):
- `Mortar::OpenFeintNewsRenderer` — in-game news overlay
- Achievement submission, leaderboard upload
- Service was shut down in 2012

**GameCenter integration**:
- Achievement and leaderboard submission
- iOS-specific, not available on other platforms

**LeaderboardManager** (~15 functions):
- `GetInstance` singleton
- Interfaces with both OpenFeint and GameCenter
- Downloads/uploads score data

**LeaderboardScreen / FriendLeaderboardItem** (~30 functions):
- UI for displaying online leaderboards
- `FriendLeaderboardItem::CollideWithButton` — touch interaction

**Related network packets**:
- `FruitSlicedPacket` — fruit slice sync for online multiplayer
- `PointsPacket` — score sync for online multiplayer
- `P2PInitializationCompleteHandler`, `P2PConnect`, `GlobalP2PMessageHandler`

**Why skip**: All online services (OpenFeint, Bada GameCenter) are defunct. P2P multiplayer requires Bada OS networking APIs. Same-screen multiplayer (split touch) is handled entirely in SlashEntity and does NOT depend on NetworkManager — it works locally and can be ported.

**What to keep**: Same-screen multiplayer uses `SlashEntity.m_SplitPoint` to divide the screen — this is purely local logic and should be preserved.

### Non-Critical UI Classes — NOT ANALYZED

The following classes exist in the binary but are minor, platform-specific stubs, or variants of already-analyzed classes. They can be ported later or skipped.

**Screens:**
- `UpsellScreen` / `UpsellScreenElement` — purchase/upgrade prompt (irrelevant without IAP)
- `AttractScreen` — attract/demo mode (may be unused in this build, only _GLOBAL__I_ exists)
- `BladeScreen` — blade preview in shop (minor, could reuse ShopScreen logic)
- `LocalScoreEntryScreen` — local score entry (Bada-specific UI)
- `VSGameOverScreen` — versus mode game-over variant
- `OptionsScreen` — settings screen (only _GLOBAL__I_ exists, possibly stub)

**Challenge mode (may not be active in this build):**
- `ChallengeScreenSL` — challenge mode screen
- `ChallengeHistoryScreenSL` — challenge history
- `CreateChallengeScreenSL` — create challenge
- `BuyStarfruitScreen` — starfruit purchase (IAP)

**Controls:**
- `KeyboardControl` — text input widget (Bada OS native keyboard)
- `ScreenButton` — likely a MenuButton alias or simplified variant
- `OperatorAlertControl` — carrier/operator notification (Bada-specific)
- `CreditCounterControl` — credit display (only _GLOBAL__I_ exists)
- `MultiplayerTutorialControl` — multiplayer-specific tutorial overlay
- `ZenVersusControl` — zen versus mode HUD widget

**MainScreen variants:**
- `MainScreenArcade` — arcade-specific main screen variant (only _GLOBAL__I_ exists, logic likely merged into MainScreen)

### Bomb::Update — DONE

Fully decompiled (195 lines). Chain-bomb spawning, fuse SFX, rotation, physics all documented in `docs/structs/entities.md`.
