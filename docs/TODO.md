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
- See `docs/entities/fruit.md`

### 3. ActorManager — DONE
- [x] `ActorManager::Add` (0x17068c) — free pool search + factory fallback, entity recycling
- [x] `ActorManager::Update` (0x1701f4) — tick all entities, deactivation queue
- [x] `ActorManager::Draw` (0x16fe7c) — render all active entities
- [x] `ActorManager::Deactivate` (0x170184) — move to free pool
- [x] `ActorManager::Remove` (0x1702d8) — destroy + erase
- [x] Struct layout (~0x106C), entity flags, vtable offsets
- See `docs/engine/actor-manager.md`

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
- See `docs/engine/sound-system.md`

### 6. Remaining Gameplay Functions
- [x] `Fruit::SetFruitType` (0x17621c, 46 lines) — set type, scale, collision sphere from FRUIT_INFO
- [x] `Fruit::EnableCollision` (0x176354, 36 lines) — toggle ColSphere on/off
- [x] `TimeModifier::ParseSpecific` (0x120100) + `UpdateSpecific` (0x1200a0) — stop/slow/ramp clock, addTime
- [x] `SlashModifier::ParseSpecific` (0x11f464) + `UpdateSpecific` (0x11f288) — blade colours, power-mask OR-accumulate into SlashEntity::s_ModPowerMask (bits gate fruit/bomb attract-repel, explosion suppress, zen mirror-bounce)
- [x] `WaveModifier::ParseSpecific` (0x12836c) + `UpdateSpecific` (0x1280e4) — fruit/bomb multipliers, overrides
- [x] `ScoreModifier::ParseSpecific` (0x11ccb0) + `UpdateSpecific` (0x11cc50) — gain/loss add+multiply
- [x] `SlashEntity::InitPoints` (0x17c340, ~40 lines) — 2 vertex buffers × (splitPoint+2) × 0x24 bytes
- [x] `Fruit::DrawShadows` (0x178f28, 33 lines) — batched shadow tri-strip via ActorManager iteration
- [x] `DrawStartFade` (0x16ab10, ~45 lines) — loading fade overlay with alpha+brightness ramp
- [x] `MainScreen::DrawPostEffects` (0x14ac94) — **no-op stub** (bx lr), skip for port
- [x] `MainScreen::UpdateScreenElements` (0x14ad3c, 55 lines) — logo bounce physics
- See `docs/entities/fruit.md`

---

## Localisation System — ANALYSED, needs port

- [x] `StringTableUtilLoadStrings` (0x0011fb20) — top-level loader, called from `InitialiseData`
- [x] `StringTableUtilLoadStringsTable` (0x0011f9dc) — language switch, builds paths, calls LoadHeader + LoadLanguage
- [x] `Mortar::StringTable::GetInfo` (0x0018a2cc) — binary search on sorted HeaderLookup entries
- [x] `Mortar::StringTable::GetString` (0x0011fec8) — resolves HeaderLookup -> StringEntry -> char*
- [x] `GETSTRING_STR` (0x0011fb40) — public key→string entrypoint, pass-through on miss
- [x] `GETSTRING_CAST_0_STR` (0x00109ec0) — wraps GETSTRING_STR(key, 0)
- [x] `.str` file format fully decoded (see `docs/engine/localisation.md`)
- [ ] **PORT TASK**: implement `Localisation::Load` / `Localisation::Get` in `src/engine/util/Localisation.{h,cpp}`
- [ ] Wire `GETSTRING_CAST_0_STR` in `src/game/ItemParseUtil.h` to call `Localisation::Get`
- [ ] Call `Localisation::Load` from `InitialiseData` using `g_GameData->languageFlag`
- See `docs/engine/localisation.md`

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

See `docs/engine/touch-input.md`. Full pipeline recovered:
- GlesForm::TransformTouchPos: raw portrait → game landscape coords (480×320, axes swapped)
- Touch::__UpdateInternal: ring buffer with TEvnt structs
- SlashEntity::TouchDown/MoveX/MoveY: maps input to entity position
- Multi-touch: 8 simultaneous touches, unique IDs

### Particle System — DONE

See `docs/engine/particles.md`. Full architecture recovered:
- PSPParticleManager: pool-based emitter management, template search by hash
- PSPParticleEmitter: 0x4C bytes, linked list, position/velocity/scale/timeScale
- PSPEmitterTemplate: loaded from data file, contains ParticleSets with spawn timing
- PSPParticle: 0xA4 bytes per particle
- AddEmitter, Update, Draw all decompiled
- 3 draw layers: -1 (background), 0 (mid), 1 (foreground)

**Still undecompiled:** AddParticle full (313 lines, only first 80 read), LoadFile, Draw full (382 lines)

### SplatEntity System — DONE

See `docs/entities/splat-entity.md`. DrawActiveSplats fully decompiled:
- Batched triangle list rendering with QUADCUSTOMVERTEX
- 6 splat variants, pool-based

**Open RE gap:** `SplatEntity::DrawSplat` (vtable, binary `0x0017f008`) — port
takes the vertex buffer pointer as an argument: `DrawSplat(QUADCUSTOMVERTEX*)`.
Binary signature may actually be `void DrawSplat()` with the buffer cursor
held in a static/global. Functionally equivalent today, but if a future
caller invokes via the vtable with the binary's exact ABI, the port's
extra argument will mismatch. Confirm via Ghidra disasm of the function
prologue (does it read `r1`, or only `r0=this`?), then either rename the
port's helper or add the static-cursor variant.

### BombFlash / BombBlast — DONE

See `docs/entities/bomb-flash.md` and `docs/entities/bomb-blast.md`. Both Update functions fully decompiled:
- BombFlash: quadratic scale + alpha fade animation (61 lines)
- BombBlast: expanding radius, dual velocity, 3s lifetime (32 lines)

### Coin System (low priority, 16 functions)

- `Coin::MakeCoins` — seen in scoring pipeline
- `Coin::ClearCoins` — seen in GameExit
- Simple bouncing coin entity for visual reward feedback

## Graphics / Rendering — Not Yet Analyzed

### High Priority

- [x] `Mortar::Model::Draw` (0x1930e0) — depth-sorted multi-mesh rendering. See `docs/engine/rendering-detail.md`
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

### Font::DrawString matrix-state leak — FIXED (2026-05-11, binary-faithful)

Found in FruitFactControl::DrawOrder. The port's `Font::DrawString` originally
built the text transform on the world MatrixStack (`world.Scale + RotZ +
LocalTranslate + Translate`) and submitted glyph verts in font-local space,
relying on GL to multiply by m_Current at draw time. If the caller left a
dirty m_Current (e.g. FFC backplate's `MakeScale(176, 176)`), the text's
own transform stacked on top -- glyphs rendered ~176x too large.

asm-inspector trace (binary @ 0x00101c58, 0x00101964): the binary's
Font_DrawString bypasses the matrix stack entirely. Per-glyph corner emit
is `Vec2 ctor` + `Vec2 operator+` writing screen-space scalar math directly
into the batch vertex slots; m_Current is never read.

**Port fix**: build the text transform matrix once after the glyph loop
(scale + rotZ + alignmentYShift + pos), apply it as a 2D affine to every
batched vertex's (x, y) on the CPU, flush with an identity world matrix.
Port now matches binary architecture (world-space verts, identity matrix
at flush) and is insensitive to the caller's m_Current state.

Other audit items left to do (proactively check the rest of the engine):
- Audit other engine functions that call `world.Push()` + a transform op;
  check whether they assume an identity baseline. Suspects: Mesh::Draw,
  ParticleManager::Draw, Model::Draw, SplatEntity::Draw, etc.

## Sound System — Mostly Analyzed (147 funcs, 25+ done)

Documented: GameSound::SFXPlay, MAMAudioThread struct, architecture diagram, BadaSound full struct + all functions (see `docs/engine/sound-system.md`).

### High Priority (needed for port)

- [x] `Mortar::SoundManager` — SFXPlay/SFXPlayInternal/SetVolume decompiled. See `functions/sound.md`
- [x] `BadaSound::SFXLoad` (0x18b1f4, 72 lines) — full .wav.pcm loading pipeline decompiled
- [x] `BadaSound::SFXPlay` (0x18b130, 20 lines) — 8-slot concurrent playback
- [x] `BadaSound::MusicPlay/Stop/Pause/Resume/Mute/SetVolume` — all decompiled
- [x] `BadaSound` struct layout — hash table (256), effects (256), 8 active slots, SFX/music volume
- [x] `Mortar::MortarSound` / `MortarSoundMAM` — all 9 concrete methods decompiled; 5 GOT thunks resolved; vtable confirmed (2 slots, dtors only); MAMAudioController backend calls mapped; `Load` inferred. See `docs/engine/sound.md`
- [x] `GameSound::Update` (0x00129380), `Pause` (0x00129256), `Unpause` (0x00129218) — decompiled; SoundSlot field12 (paused-by-system) and m_field04 (deferred-unpause) confirmed. See `docs/engine/sound.md`
- [x] `MAMAudioController::PlaySound/StopSound/PauseSound/ResumeSound/SetSoundVolume/LoadSound` — all 6 fully decompiled. See `docs/engine/sound.md`

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

64-bit LCG with Knuth MMIX multiplier. Full algorithm, constants, and port-ready C code in `docs/engine/rng.md`.
- Rand32: 64-bit LCG step, upper-32-bit output, multiply-high range reduction
- RandF: Rand32(0x7FFFF) / 524287.0 * max
- Seed: 0xDEADBEEF, Multiplier: 0x5D588B656C078965, Increment: 0x269EC3

### StringHash Algorithm — DONE

Full C implementation saved to `docs/engine/string-hash.md`. Jenkins lookup3 with case-folding, initial `c = 0x805 + len`.

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

### Non-Critical UI Classes — TRIAGED

RE confirmed 13 of 15 candidates are **phantoms** (only `_GLOBAL__I_*.cpp` TU-init exists in binary; class instance methods were dead-code-stripped). Two have real instance code and are stubbed.

**Stubbed (defunct, no-op port):**
- `UpsellScreen` / `UpsellScreenElement` — `src/screens/UpsellScreen.h`; binary ctor @ 0x00164814, sizeof 0x1EC. Immediately-dismissed stub; IAP irrelevant on port.
- `KeyboardControl` — `src/hud/KeyboardControl.h`; binary ctor @ 0x0014649c, sizeof 0xD4. Port specific: Bada native keyboard bypassed; SDL_StartTextInput() if ever needed.

**Phantoms — omitted from port** (class has TU init but no instance methods; dead-code-stripped):
- `AttractScreen` — phantom
- `BladeScreen` — phantom
- `LocalScoreEntryScreen` — phantom
- `VSGameOverScreen` — phantom
- `OptionsScreen` — phantom
- `ChallengeScreenSL` — phantom
- `ChallengeHistoryScreenSL` — phantom
- `CreateChallengeScreenSL` — phantom
- `BuyStarfruitScreen` — phantom
- `OperatorAlertControl` — phantom
- `CreditCounterControl` — phantom
- `MultiplayerTutorialControl` — phantom
- `ZenVersusControl` — phantom

### Bomb::Update — DONE

Fully decompiled (195 lines). Chain-bomb spawning, fuse SFX, rotation, physics all documented in `docs/entities/bomb.md`.
