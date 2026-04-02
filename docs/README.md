# FruitNinja.exe Reverse Engineering Documentation

**Binary:** ARM32 Little-Endian ELF (Samsung Bada OS / webOS homebrew)
**Engine:** Halfbrick Mortar Engine
**Screen:** 320x480 portrait
**Entry:** `OspMain`

## Overview

- [classes.md](classes.md) — Class hierarchy and method listing (~70 classes)
- [port-plan.md](port-plan.md) — Symbol coverage, gap analysis, porting roadmap
- [resources.md](resources.md) — Asset directory structure, XML schemas, loading flow
- [formats/textures.md](formats/textures.md) — .tex format: 12-byte header, RGBA4444/RGB565 pixel data
- [formats/audio.md](formats/audio.md) — .wav.pcm format: 20-byte header, 16-bit mono PCM @ 16kHz
- [formats/models.md](formats/models.md) — .mad/.mmd HBR0 container format (Halfbrick proprietary)
- [formats/fonts.md](formats/fonts.md) — .fnt BMFont text format (standard)
- [TODO.md](TODO.md) — Remaining RE gaps (mostly complete)

## Struct Layouts

Detailed field-by-field layouts for all recovered structs.

- [structs/entities.md](structs/entities.md) — MortarEntity, Fruit, Bomb, SlashEntity
- [structs/game.md](structs/game.md) — Game singleton, FruitNinja app, GlesForm
- [structs/camera.md](structs/camera.md) — MortarCamera, FruitCamera
- [structs/wave.md](structs/wave.md) — WaveManager, WaveInfo
- [structs/data.md](structs/data.md) — FRUIT_INFO (816 bytes), FruitSaveData
- [structs/hud.md](structs/hud.md) — HUD, HUDControl, MissControl
- [structs/other.md](structs/other.md) — ScoreModifier, MAMAudioThread, ItemManager, PowerUpManager, BonusManager
- [structs/gameplay-misc.md](structs/gameplay-misc.md) — Coin, SlashEntityGhost, MenuButton, MenuBackground, EffectImage, QUADCUSTOMVERTEX
- [structs/ui-widgets.md](structs/ui-widgets.md) — FruitFactControl, ScrollingMenu, ScoreControl, TimeControl, SpeedControl, etc. (10 classes)
- [structs/screens.md](structs/screens.md) — DojoScreen, ShopScreen, PauseScreen, GameOverScreen, GameModeScreen, AboutScreen (6 classes)

## Systems

Decompiled game systems with pseudocode and flow diagrams.

- [systems/state-machine.md](systems/state-machine.md) — 3-state task machine (Splash, Frontend, Game) + internal sub-states
- [systems/rendering.md](systems/rendering.md) — Full GameDraw pipeline, HUD layers, Fruit::Draw, effects
- [systems/physics.md](systems/physics.md) — Ballistic flight, two-body slicing, gravity ramp, spawn pipeline
- [systems/wave-system.md](systems/wave-system.md) — Wave XML format, WAVE_INFO/SPAWNER_INFO parsing, wave progression
- [systems/scoring.md](systems/scoring.md) — Score pipeline from slice to AddToCurrentScore, combo system
- [systems/menu-flow.md](systems/menu-flow.md) — Screen hierarchy, callbacks, mode selection
- [systems/save-system.md](systems/save-system.md) — FruitSaveData persistence, stat tracking
- [systems/sound-system.md](systems/sound-system.md) — GameSound pool, BadaSound backend, MAMAudioThread
- [systems/string-hash.md](systems/string-hash.md) — Jenkins lookup3 hash (C implementation)
- [systems/power-ups.md](systems/power-ups.md) — PowerUp struct, 4 modifier types, activation flow
- [systems/particles.md](systems/particles.md) — PSPParticleManager/Emitter/Particle architecture
- [systems/touch-input.md](systems/touch-input.md) — Touch coordinate transform, input pipeline to SlashEntity
- [systems/effects.md](systems/effects.md) — SplatEntity, BombFlash, BombBlast visual effects

## Ghidra Scripts

Located in `ghidra_scripts/`:

| Script | Purpose |
|--------|---------|
| ApplyStructs.java | Creates 25 struct types in `/FruitNinja/` category |
| ApplyPrototypes.java | Applies struct types to 53 function signatures |
| ReplaceTypes.java | Replaces Demangler types with our typed versions |
| FindTextFunctions.java | Lists all 9,624 non-thunk .text functions |
| ListClasses.java | Extracts class hierarchy from symbols |

## Key Stats

- **9,624** non-thunk functions in .text section
- **96.5%** symbol coverage (named functions)
- **323** T.NNN helpers identified and renamed
- **25** struct types defined in Ghidra
- **53** function prototypes applied
