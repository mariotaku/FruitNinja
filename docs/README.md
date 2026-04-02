# FruitNinja.exe Reverse Engineering Documentation

**Binary:** ARM32 Little-Endian ELF (Samsung Bada OS / webOS homebrew)
**Engine:** Halfbrick Mortar Engine
**Screen:** 320x480 portrait
**Entry:** `OspMain`

## Overview

- [class-status.md](class-status.md) — **All 118 classes**: analysis status, sizes, descriptions, doc links
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
- [structs/data-classes.md](structs/data-classes.md) — FNHighscore, FNHighscoreList, Bonus, BonusType, BonusAwardHud
- [structs/gameplay-misc.md](structs/gameplay-misc.md) — Coin, SlashEntityGhost, MenuButton, MenuBackground, EffectImage, QUADCUSTOMVERTEX
- [structs/ui-widgets.md](structs/ui-widgets.md) — FruitFactControl, ScrollingMenu, ScoreControl, TimeControl, SpeedControl, etc. (10 classes)
- [structs/ui-controls2.md](structs/ui-controls2.md) — BonusScreen, ScreenFadeControl, ScreenTint, ComboControl, NotificationControl, ProgressionTimerControl, GenericHUDControl (7 classes)
- [screens/](screens/) — Screen classes (split per screen):
  - [main.md](screens/main.md) — MainScreen (dojo menu hub, 675-line Update, 0x120 bytes)
  - [game-mode.md](screens/game-mode.md) — GameModeScreen (mode selection)
  - [game-over.md](screens/game-over.md) — GameOverScreen (results, 529-line Update)
  - [pause.md](screens/pause.md) — PauseScreen (in-game pause, multiplayer support)
  - [shop.md](screens/shop.md) — ShopScreen (blade shop)
  - [dojo.md](screens/dojo.md) — DojoScreen
  - [about.md](screens/about.md) — AboutScreen
  - [common-patterns.md](screens/common-patterns.md) — BaseScreen, button creation, transitions

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
- [systems/rendering-detail.md](systems/rendering-detail.md) — Model::Draw, TintWhite/Colour, SetupQuad/AddQuad, Font::Load, QUADCUSTOMVERTEX

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
