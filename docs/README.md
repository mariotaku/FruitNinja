# FruitNinja.exe Reverse Engineering Documentation

**Binary:** ARM32 Little-Endian ELF (Samsung Bada OS / webOS homebrew)
**Engine:** Halfbrick Mortar Engine
**Screen:** 480x320 landscape (rendered on portrait Bada device with 90° rotation in touch/camera)
**Entry:** `OspMain`

## Overview

- [class-status.md](class-status.md) — **All 129 classes**: analysis status, sizes, descriptions, doc links
- [classes.md](classes.md) — Class hierarchy and method listing (~70 classes)
- [port-plan.md](port-plan.md) — Symbol coverage, gap analysis, porting roadmap
- [resources.md](resources.md) — Asset directory structure, XML schemas, loading flow
- [source-files.md](source-files.md) — **All 142 original .cpp source files** with addresses, categories, and port status
- [TODO.md](TODO.md) — Remaining RE gaps (mostly complete)

---

## Mortar Engine (Halfbrick)

Reusable engine subsystems — not specific to FruitNinja.

### Engine Systems & Structs

- [engine/system-manager.md](engine/system-manager.md) — SystemManager: FPS ring buffer, quit lifecycle (212 bytes, 11 fields)
- [engine/display-manager.md](engine/display-manager.md) — DisplayManager singleton (GL state, frame setup, screen rotation, texture filtering), DisplayManagerBada subclass
- [engine/matrix-manager.md](engine/matrix-manager.md) — MatrixManager (4 matrix stacks, dirty-tracking), MatrixStack, Matrix44 methods, HUD draw pipeline wrappers
- [engine/camera.md](engine/camera.md) — MortarCamera, FruitCamera, ortho projection setup
- [engine/coordinate-system.md](engine/coordinate-system.md) — Binary centred ortho, 90° screen rotation, dead HUDControl3d offset, port mixed-convention audit
- [engine/touch-rewrite-plan.md](engine/touch-rewrite-plan.md) — Detailed plan for rewriting the touch input layer to match the binary's poll-based `Mortar::Touch` architecture
- [engine/input-manager.md](engine/input-manager.md) — Action-based input system, callback registration, config parsing, 16-touch
- [engine/touch-input.md](engine/touch-input.md) — Touch coordinate transform (portrait→landscape), input pipeline to SlashEntity
- [engine/touch-system.md](engine/touch-system.md) — Mortar::Touch internals: double-buffered 8-slot state, TEvnt ring buffer, data flow
- [engine/sound-system.md](engine/sound-system.md) — GameSound pool (32 slots), BadaSound backend, MAMAudioThread (16 voices, 16kHz)
- [engine/audio-internals.md](engine/audio-internals.md) — MAMAudioController command protocol, MAMVoice/MAMSound, NLFQueue, threading primitives
- [engine/particles.md](engine/particles.md) — PSPParticleManager, Emitter, Particle architecture, PSPParticleEmitter rotation pair
- [engine/particle-refine-notes.md](engine/particle-refine-notes.md) — Detailed particle system internals
- [engine/actor-manager.md](engine/actor-manager.md) — ActorManager: Add, Update, Draw, Deactivate, Remove, entity lifecycle
- [engine/mesh.md](engine/mesh.md) — Mesh (0x7C): struct, vtable (11 entries), BoneBinding, SharedPropsInfo, Draw, LoadMesh parser, effect properties
- [engine/mesh-port-status.md](engine/mesh-port-status.md) — Mesh/MeshManager port vs binary comparison, remaining gaps checklist
- [engine/effect-system.md](engine/effect-system.md) — EffectProperty/EffectPropertyList/SharedEffectProperties system: struct layouts, GetProperty, SetValue, full LoadMesh→Draw→GL flow
- [engine/texture-mesh-manager.md](engine/texture-mesh-manager.md) — TextureManager (24-byte cache), MeshManager (20 bytes), loading pipelines, ARM struct-return convention
- [engine/string-hash.md](engine/string-hash.md) — Jenkins lookup3 hash (C implementation)
- [engine/rng.md](engine/rng.md) — Math::Random: 64-bit LCG (Knuth MMIX), Rand32/RandF, constants
- [engine/other-structs.md](engine/other-structs.md) — MAMAudioThread, engine subsystem singleton list
- [engine/utility-types.md](engine/utility-types.md) — ResourceLoader (68B, HBR0 parser), SmartPtr (4B, intrusive refcount), Delegate0-4 (36B, callback system)

### Engine Rendering

- [engine/rendering-pipeline.md](engine/rendering-pipeline.md) — Two-path rendering: 3D (Effect/Geometry/PassBinding/glDraw) vs 2D (DrawQuad/DrawTriList), Font::DrawString, BakedString
- [engine/rendering-detail.md](engine/rendering-detail.md) — Model::Draw, TintWhite/Colour, SetupQuad/AddQuad, Font::Load, QUADCUSTOMVERTEX
- [engine/rendering-functions.md](engine/rendering-functions.md) — HUDControl3d::Draw, Model::Draw, Tint, Quad builders
- [engine/vtables.md](engine/vtables.md) — DisplayManager (20 entries), HUDControl (15 entries, confirmed), Entity (7 entries) vtable layouts
- [engine/assets.md](engine/assets.md) — GPUafyTexture, TexFmtToGL, LoadVertexStreamPSP

### Asset Formats

- [engine/formats/textures.md](engine/formats/textures.md) — .tex format: 12-byte header, RGBA4444/RGB565 pixel data
- [engine/formats/audio.md](engine/formats/audio.md) — .wav.pcm format: 20-byte header, 16-bit mono PCM @ 16kHz
- [engine/formats/models.md](engine/formats/models.md) — .mad/.mmd HBR0 container format (Halfbrick proprietary)
- [engine/formats/fonts.md](engine/formats/fonts.md) — .fnt BMFont text format (standard)

---

## FruitNinja Game

Game-specific logic, screens, entities, and systems.

### Entities

- [entities/entity-base.md](entities/entity-base.md) — Mortar::Entity (0x3C), CreateEntity factory
- [entities/fruit.md](entities/fruit.md) — Fruit entity (0x118): struct, vtable, Update, CollisionResponse, Draw, Init, Chuck, LoadInfo
- [entities/bomb.md](entities/bomb.md) — Bomb entity (0xB0): struct, vtable, collision radius formula verified
- [entities/slash-entity.md](entities/slash-entity.md) — SlashEntity (0x184): blade trail, collision, combo tracking, ghost effect
- [entities/coin.md](entities/coin.md) — Coin entity (0x94): struct, vtable (10 entries), state machine (5 states), physics, InitCoin, MakeCoins, Update, Draw
- [entities/bomb-blast.md](entities/bomb-blast.md) — BombBlast (0x70): bomb explosion effect
- [entities/bomb-flash.md](entities/bomb-flash.md) — BombFlash: bomb flash overlay
- [entities/splat-entity.md](entities/splat-entity.md) — SplatEntity (0x78): full struct layout, velocity transform, type selection, UV atlas
- [entities/miss-control.md](entities/miss-control.md) — MissControl: critical/rare overlay labels, MakeCritical, MakeRare

### Game-Specific Engine Features

- [engine/fruit-size.md](engine/fruit-size.md) — Collision radius formula: `radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam`
- [engine/fruit-slice-notes.md](engine/fruit-slice-notes.md) — Fruit slicing: CollisionResponse, Slice, impulse, critical, splat spawning, emitters, SFX, coins, particles
- [engine/critical-flash.md](engine/critical-flash.md) — CriticalFlash function: full-screen tint for critical/special slices

### Game Functions

- [functions/game-loop.md](functions/game-loop.md) — Entry point chain, OnTimerExpired, GameTaskUpdate, GameDraw
- [functions/game-update.md](functions/game-update.md) — GameUpdate (State 2): full call tree, time scaling, bomb/wave/retry
- [functions/game-flow.md](functions/game-flow.md) — HitBomb, QuitToMenu, GameOver, touch input
- [functions/wave.md](functions/wave.md) — WaveManager::Init, SpawnFruit, SpawnBomb, CriticalChance
- [functions/scoring.md](functions/scoring.md) — AddToCurrentScore, StringHash
- [functions/power-ups.md](functions/power-ups.md) — ActivatePower, Activate, Parse
- [functions/particles.md](functions/particles.md) — AddEmitter, Emitter::Update, Manager::Update/Draw
- [functions/data-parsing.md](functions/data-parsing.md) — XML loaders: powerups, bonus, items, achievements, particles
- [functions/sound.md](functions/sound.md) — GameSound::SFXPlay
- [functions/screens-effects.md](functions/screens-effects.md) — Screen callbacks (effect functions moved to entities/)
- [functions/entity-factory-combo-timekeeper.md](functions/entity-factory-combo-timekeeper.md) — EntityFactory (5 types), ComboChecker (COMBO_TYPE enum, pattern matching), TimeKeeper/TimeModifier/TimeControl, FruitCamera shake/follow

### Game Systems

- [systems/state-machine.md](systems/state-machine.md) — 3-state task machine (Splash, Frontend, Game) + internal sub-states
- [systems/rendering.md](systems/rendering.md) — Full GameDraw pipeline, HUD layers, Fruit::Draw, effects
- [systems/physics.md](systems/physics.md) — Ballistic flight, two-body slicing, gravity ramp, spawn pipeline
- [systems/wave-system.md](systems/wave-system.md) — Wave XML format, WAVE_INFO/SPAWNER_INFO parsing, wave progression
- [systems/scoring.md](systems/scoring.md) — Score pipeline from slice to AddToCurrentScore, combo system
- [systems/menu-flow.md](systems/menu-flow.md) — Screen hierarchy, callbacks, mode selection
- [systems/save-system.md](systems/save-system.md) — FruitSaveData persistence, stat tracking
- [systems/power-ups.md](systems/power-ups.md) — PowerUp struct, 4 modifier types, activation flow

### Game Struct Layouts
- [structs/game.md](structs/game.md) — Game singleton, MortarGame vtable, FruitNinja app, GlesForm
- [structs/hud.md](structs/hud.md) — HUD, HUDControl, HUDControl3d class hierarchy, MissControl
- [structs/wave.md](structs/wave.md) — WaveManager, WaveInfo
- [structs/data.md](structs/data.md) — FRUIT_INFO (816 bytes), FruitSaveData
- [structs/gameplay-misc.md](structs/gameplay-misc.md) — MenuButton (38 callers, TouchReleased gate, MakeCritical/MakeRare), MenuBackground, EffectImage, QUADCUSTOMVERTEX
- [structs/ui-widgets.md](structs/ui-widgets.md) — FruitFactControl, ScrollingMenu, ScoreControl, TimeControl, SpeedControl, etc. (10 classes)
- [structs/ui-controls2.md](structs/ui-controls2.md) — BonusScreen, ScreenFadeControl, ScreenTint, ComboControl, NotificationControl, GenericHUDControl (7 classes)
- [structs/data-classes.md](structs/data-classes.md) — FNHighscore, FNHighscoreList, Bonus, BonusType, BonusAwardHud
- [structs/game-managers.md](structs/game-managers.md) — ScoreModifier, ItemManager, PowerUpManager, BonusManager, asset loading order

### Screens

- [screens/main.md](screens/main.md) — MainScreen (dojo menu hub, 675-line Update, 0x120 bytes)
- [screens/game-mode.md](screens/game-mode.md) — GameModeScreen (mode selection)
- [screens/game-over.md](screens/game-over.md) — GameOverScreen (results, 529-line Update)
- [screens/pause.md](screens/pause.md) — PauseScreen (in-game pause, multiplayer support)
- [screens/shop.md](screens/shop.md) — ShopScreen (blade shop)
- [screens/dojo.md](screens/dojo.md) — DojoScreen
- [screens/about.md](screens/about.md) — AboutScreen
- [screens/common-patterns.md](screens/common-patterns.md) — BaseScreen, button creation, transitions
- [screens/layout-positions.md](screens/layout-positions.md) — All screen element positions (verified from binary via read_memory)

---

## Ghidra Scripts

Located in `ghidra_scripts/`:

| Script | Purpose |
|--------|---------|
| FN01_ApplyStructs.java | Core structs: Entity, Fruit, Bomb, SlashEntity, Game, etc. |
| FN02_ApplyStructs2.java | Particle, effects, PowerUp, GameSound, touch input structs |
| FN03_ApplyStructs3.java | Mesh/model: LegacyPSPVertexDecl, IVertexSource, HBR0Header |
| FN04_ReplaceTypes.java | Replace Demangler types with /FruitNinja/ typed versions |
| FN05_ApplyPrototypes.java | Apply struct types to 70+ function signatures |
| FN08_UpdateStructs.java | Updated structs: OspPoint, FruitNinjaApp, FRUIT_INFO, BadaSound |
| CreateMatrixStructs.java | MatrixStack + MatrixManager struct creation |

## Key Stats

- **9,624** non-thunk functions in .text section
- **96.5%** symbol coverage (named functions)
- **25+** struct types defined in Ghidra
- **70+** function prototypes applied
