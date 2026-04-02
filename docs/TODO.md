# Reverse Engineering TODO

## Unfinished Analysis

### Power-Up System — MOSTLY DONE

See `docs/systems/power-ups.md`. Core architecture recovered:
- PowerUp struct (~0xB8 bytes): name, hash, modifiers list, textures, screen effect
- 4 modifier types: ScoreModifier, TimeModifier, SlashModifier, WaveModifier
- ActivatePower + Activate flow fully traced
- PowerUpManager maps and active list documented

**Still undecompiled** (lower priority):
- `PowerUpManager::Update` — tick active power timers
- `PowerUpManager::Load` — load all power-up XML definitions
- `PowerUp::Deactivate` — cleanup on expiry
- Individual modifier Parse/UpdateSpecific methods
- `ScreenEffect::Activate/Parse` — visual effect details

### Touch → Slash Input Pipeline — DONE

See `docs/systems/touch-input.md`. Full pipeline recovered:
- GlesForm::TransformTouchPos: raw → game coords (320×480, Y-flipped)
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

## Sound System — Partially Analyzed (147 funcs, 13 done)

Documented: GameSound::SFXPlay, MAMAudioThread struct, architecture diagram (see `docs/systems/sound-system.md`).

### High Priority (needed for port)

- [x] `Mortar::SoundManager` — SFXPlay/SFXPlayInternal/SetVolume decompiled. See `functions/sound.md`
- [ ] `Mortar::MortarSound` (15 funcs) — individual sound instance: SetVolume, IsPlaying, Stop. MortarSoundMAM = 0x10 bytes.
- [ ] `BadaSound::SFXLoad` (0x18b1f4) — how .wav.pcm files are loaded. Need to understand for SDL2 audio replacement.
- [ ] `BadaSound::MusicPlay/Stop/Pause/Resume` — background music control

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
