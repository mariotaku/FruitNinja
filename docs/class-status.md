# Class Analysis Status

Status: ✅ = fully analyzed, 🔶 = partially analyzed, ❌ = not analyzed, ⏭️ = intentionally skipped

## Gameplay Entities

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| Mortar::Entity (MortarEntity) | ✅ | 0x3c | Base entity class: pos, vel, angle, collision | structs/entities.md |
| Fruit | ✅ | 0x118 | Fruit entity: slicing, physics, two-body split | structs/entities.md |
| Bomb | ✅ | 0xac | Bomb entity: fuse timer, chain spawn, rotation | structs/entities.md |
| SlashEntity | ✅ | 0x184 | Blade/swipe: trail, collision, combo tracking | structs/entities.md |
| SlashEntityGhost | ✅ | ~0x10 | Fading blade echo (alpha decay on vertex colours) | structs/gameplay-misc.md |
| SplatEntity | ✅ | 0x78 | Juice splat pool: 6 variants, batched triangle list | systems/effects.md |
| SplatEffect | ✅ | ~0x1c | Simple textured quad overlay for splats | systems/rendering-detail.md |
| BombFlash | ✅ | 0x44 | Bomb hit flash: quadratic scale + alpha animation | systems/effects.md |
| BombBlast | ✅ | 0x70 | Bomb explosion: expanding radius, 3s lifetime | systems/effects.md |
| Coin | ✅ | ~0x70 | Bouncing reward coin: state machine + ballistics | structs/gameplay-misc.md |
| MenuBackground | ✅ | 0x08 | Simple background texture | structs/gameplay-misc.md |

## Game & Application

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| Game | ✅ | 0x608 | Game singleton: score, timers, pointers to all subsystems | structs/game.md |
| GameTaskState | ✅ | 0x118 | Per-task state: slash entities, effects, textures, flags | structs/game.md |
| FruitNinja | ✅ | 0x48 | Bada app: EGL context, 10ms timer loop | structs/game.md |
| GlesForm | ✅ | 0x1f8 | Bada GL form: touch input, 8-finger multitouch | structs/game.md |
| FruitCamera | ✅ | 0x16c | Camera: idle/follow mode, shake system | structs/camera.md |
| MortarCamera | ✅ | 0x12c | Base camera: 4 matrices, pos/lookAt/up | structs/camera.md |

## Wave / Spawn System

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| WaveManager | ✅ | 0x2d8 | Wave singleton: speed, critical chance, wave loading | structs/wave.md, systems/wave-system.md |
| WAVE_INFO | ✅ | 0x78 | Per-wave: score threshold, spawners, bomb params | systems/wave-system.md |
| DEFAULT_WAVE_INFO | ✅ | 0x40 | Default wave values per game mode | systems/wave-system.md |
| SPAWNER_INFO | ✅ | 0x64 | Spawn rule: fruit types, angles, speed, gravity | systems/wave-system.md |
| COIN_CHANCEINATOR | ✅ | 0x08 | Coin spawn probability | systems/wave-system.md |
| PROBABILITY_OVERIDE | ❌ | — | Wave probability override | — |
| WaveQue | 🔶 | — | Wave queue container | — |
| WaveQueItem | 🔶 | — | Wave queue entry | — |
| WaveState | ❌ | — | Wave save state | — |

## Data / Asset Structs

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| FRUIT_INFO | ✅ | 0x330 | Per-fruit: name, hashes, score, sounds, power-ups | structs/data.md |
| FruitModelInfo | ✅ | 0x24 | Per-fruit: 6 model SmartPtrs + 2 effect props | structs/gameplay-misc.md |
| FruitSaveData | ✅ | 0x238 | Save data: stats maps, achievements, scores | structs/data.md, systems/save-system.md |
| ImpactSound | ✅ | 0x0c | Sound name + weighted random | structs/data.md |
| FRUIT_POWER | ✅ | 0x0c | Power-up hash + weighted random | structs/data.md |
| FRUIT_POWERS | ✅ | 0x08 | Power-up array container | structs/data.md |
| ItemInfo | 🔶 | 0x40 | Shop item base (key, type, achieveId) | structs/other.md |
| SlashModInfo | ❌ | 0x110 | Blade modifier item (extends ItemInfo) | — |
| EntityState | ❌ | — | Entity save state for FruitSaveData | — |

## HUD & UI Controls

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| HUD | ✅ | 0x24 | HUD container: list\<HUDControl*\>, 6 scale floats | structs/hud.md |
| HUDControl | ✅ | 0x60 | Base widget: pos, size, alpha, removal callback | structs/hud.md |
| HUDControl3d | ✅ | ~0x7C | 3D HUD base: texture, UV rect, rotation Draw | structs/hud.md |
| MissControl | ✅ | 0x94 | Combo text popup pool (9 sprites) | structs/hud.md |
| ScoreControl | ✅ | 0x100 | Score display: 16-digit alpha, wobble, pulse | structs/ui-widgets.md |
| TimeControl | ✅ | 0x108+ | Countdown timer: colour warnings, freeze support | structs/ui-widgets.md |
| SpeedControl | ✅ | 0xac | Combo speed gauge with particles | structs/ui-widgets.md |
| TutorialControl | ✅ | 0x98 | Tutorial overlay: multi-phase animation | structs/ui-widgets.md |
| CoinCounter | ✅ | 0x94 | Coin display (Update = no-op) | structs/ui-widgets.md |
| FruitFactControl | ✅ | 0x204 | Game-over fact display with leaderboard | structs/ui-widgets.md |
| ScrollingMenu | ✅ | 0x100 | Touch-scrolling menu | structs/ui-widgets.md |
| ScrollingMenuItem | ✅ | 0x58 | Menu list item with callback | structs/ui-widgets.md |
| SliderControl | ✅ | 0xc0 | Touch slider (min/max/step) | structs/ui-widgets.md |
| VerticalScroller | ✅ | 0xa8 | Value scroller (up/down touch) | structs/ui-widgets.md |
| GenericHUDControl | ✅ | 0x1c8 | Animated HUD: 4× TransitionInfo + 4× PulseInfo | structs/ui-controls2.md |
| BonusScreen | ✅ | 0xc8 | Post-game bonus awards display | structs/ui-controls2.md |
| ScreenFadeControl | ✅ | 0xbc | Full-screen fade with callback | structs/ui-controls2.md |
| ScreenTint | ✅ | 0x28 | Power-up tint (value type, from XML) | structs/ui-controls2.md |
| ComboControl | ✅ | 0x8c | Combo counter: 1s lifetime, "x3" text | structs/ui-controls2.md |
| NotificationControl | ✅ | 0x110 | 3-type notification with slide animation | structs/ui-controls2.md |
| ProgressionTimerControl | ✅ | 0x110 | Countdown with fade + delegate | structs/ui-controls2.md |
| ScoreMultiplyerBoard | ✅ | ~0x9c | Arcade x2 popup via Font::DrawString | systems/rendering-detail.md |
| MenuButton | ✅ | 0x15c | Interactive button with sub-pieces | structs/gameplay-misc.md |
| ScreenButton | ❌ | — | Simplified button variant | — |
| ScreenEffect | ✅ | 0x50 | Power-up screen effect (images + tint) | systems/power-ups.md |
| EffectImage | 🔶 | — | Screen effect image (parsed from XML) | structs/gameplay-misc.md |

## Screens

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| MainScreen | ✅ | 0x120 | Main menu: 6 buttons, state machine, 13 textures | screens/main.md |
| GameModeScreen | ✅ | ~0xd0 | Mode selection: Classic/Arcade/Zen buttons | screens/game-mode.md |
| GameOverScreen | ✅ | ~0x13c | Results: 14+ states, score submit, achievements | screens/game-over.md |
| PauseScreen | ✅ | ~0xd8 | Pause overlay: multiplayer support | screens/pause.md |
| ShopScreen | ✅ | ~0xbc | Blade shop: buy/equip | screens/shop.md |
| DojoScreen | ✅ | ~0xa4 | Dojo hub (secondary menu) | screens/dojo.md |
| AboutScreen | ✅ | ~0xa0 | Credits display | screens/about.md |
| BaseScreen | ✅ | — | Shared base: transition alpha, state field | screens/common-patterns.md |
| UpsellScreen | ⏭️ | — | Purchase prompt (IAP, irrelevant) | docs/TODO.md |
| LeaderboardScreen | ⏭️ | — | Online leaderboard (defunct) | docs/TODO.md |
| VSGameOverScreen | ⏭️ | — | Versus game-over variant | docs/TODO.md |
| AttractScreen | ⏭️ | — | Attract/demo mode (possibly unused) | docs/TODO.md |
| BladeScreen | ⏭️ | — | Blade preview (minor) | docs/TODO.md |
| LocalScoreEntryScreen | ⏭️ | — | Bada-specific score entry | docs/TODO.md |
| OptionsScreen | ⏭️ | — | Settings (possibly stub) | docs/TODO.md |

## Power-Up System

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| PowerUp | ✅ | ~0xb8 | Power-up template: name, modifiers, textures | systems/power-ups.md |
| PowerUpManager | ✅ | ~0x90 | Singleton: maps, active list, DtMod, score mults | systems/power-ups.md |
| PowerUpShop | ✅ | — | In-game power-up purchase UI | systems/rendering-detail.md |
| ScoreModifier | ✅ | 0x3c | Score gain/loss multiplier | structs/other.md |
| TimeModifier | 🔶 | 0x3c | Time scale modifier (Frenzy) | systems/power-ups.md |
| SlashModifier | 🔶 | 0x40 | Blade behavior modifier | systems/power-ups.md |
| WaveModifier | 🔶 | 0x44 | Wave spawn modifier | systems/power-ups.md |
| GameModifier | 🔶 | — | Base class for all modifiers | systems/power-ups.md |
| PurchaseInfo | ✅ | 0xc4 | IAP purchase data | — |

## Sound System

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| GameSound | ✅ | 0x708 | Pool-based SFX manager (32 slots) | systems/sound-system.md |
| Mortar::SoundManager | ❌ | — | Engine audio singleton (35 funcs) | — |
| Mortar::MortarSound | ❌ | — | Individual sound handle (15 funcs) | — |
| BadaSound | ❌ | — | Bada platform audio (22 funcs) | — |
| MAMAudioController | ❌ | — | Audio subsystem bridge (18 funcs) | — |
| MAMAudioThread | 🔶 | 0x154 | Mixing thread: 16kHz, 16 voices, NLFQueue | structs/other.md |
| MortarAudioMixerBada | ❌ | — | Bada PCM mixer (17 funcs) | — |
| SoundEffect / SoundEffectBada | ❌ | — | Sound effect wrapper (61 funcs) | — |

## Collision

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| ColLine | ✅ | 0x20 | Line segment (blade collision) | structs/entities.md |
| ColSphere | ✅ | 0x18 | Sphere (fruit collision) | structs/entities.md |
| Col | 🔶 | — | Base collision shape | — |
| ColAABB | ❌ | — | Axis-aligned bounding box | — |

## Rendering Engine

| Class | Status | Size | Description | Doc |
|-------|--------|------|-------------|-----|
| DisplayManager | ✅ | ~0x54 | GL state singleton: viewport, colours, light | systems/rendering-detail.md |
| MatrixManager | ✅ | — | Matrix operations (known from T.NNN renames) | — |
| MatrixStack | ✅ | — | At MatrixManager+0x1094 | — |
| Model | ✅ | — | Depth-sorted multi-mesh draw | systems/rendering-detail.md |
| Mesh | 🔶 | — | AddGeometry, Draw, DrawQuadUnCached decompiled | formats/models.md |
| Font | ✅ | 0x438 | BMFont loader + DrawString | systems/rendering-detail.md |
| GeometryBinding_Bada | ✅ | — | PassBinding::Apply fully decompiled | formats/models.md |
| Texture / Texture2D | ✅ | — | .tex loader, GPUafyTexture, TexFmtToGL | formats/textures.md |
| EffectGroup / EffectProperty | ❌ | — | ES 1.x effect system (186 funcs, skip for port) | — |
| ReloadableTexture | ❌ | — | Hot-reload texture wrapper | — |

## Mortar Engine Core

| Class | Status | Description | Doc |
|-------|--------|-------------|-----|
| ActorManager | ✅ | Entity pool: Add, Update, Draw, Deactivate, Remove. Free pool recycling. | functions/actor-manager.md |
| ResourceLoader | ✅ | HBR0 container parser | formats/models.md |
| DataReader / FileDataReader | 🔶 | Binary read helpers | — |
| InputManager | 🔶 | Input callback registration | — |
| Touch | ✅ | Ring buffer for touch events | systems/touch-input.md |
| PSPParticleManager | ✅ | Particle system singleton | systems/particles.md |
| PSPParticleEmitter | ✅ | Emitter instance | systems/particles.md |
| PSPEmitterTemplate | ✅ | Particle template (from XML) | systems/particles.md |
| SystemManager | ❌ | Engine bootstrap | — |
| FileManager | ❌ | File I/O abstraction | — |
| NetworkManager | ⏭️ | P2P + OpenFeint (defunct) | docs/TODO.md |
| ItemManager | 🔶 | Item/blade shop data | structs/other.md |
| AchievementManager | 🔶 | Achievement tracking | — |
| BonusManager | ✅ | Bonus award data | structs/other.md |
| LeaderboardManager | ⏭️ | Online leaderboards (defunct) | docs/TODO.md |

## Math Types

| Class | Status | Description |
|-------|--------|-------------|
| _Vector2\<float\> | ✅ | 2D vector |
| _Vector3\<float\> | ✅ | 3D vector |
| _Quaternion\<float\> | ✅ | Rotation quaternion |
| _Matrix33\<float\> | 🔶 | 3×3 matrix |
| _Matrix43\<float\> | 🔶 | 4×3 matrix |
| _Matrix44\<float\> | ✅ | 4×4 matrix |
| Math::Random | ✅ | 64-bit LCG (Knuth MMIX), 24 bytes. See [rng](systems/rng.md) |
| Colour | ✅ | BGRA packed 4 bytes |

## UI Widgets (additional)

| Class | Status | Funcs | Description |
|-------|--------|-------|-------------|
| ComboBox | ❌ | 22 | Dropdown/combo box widget (generic UI) |
| ListBox | ❌ | — | List selection widget (generic UI) |
| CheckBox | ❌ | — | Toggle checkbox widget (generic UI) |
| MenuButtonAddOn | ❌ | 1 | Sub-element for MenuButton |
| BonusAwardHud | ✅ | 4 | Bonus award display (0x48 bytes, value type) | structs/data-classes.md |
| FPSCounter | ❌ | 8 | Debug FPS display (likely dev-only) |

## Score / Leaderboard Data

| Class | Status | Funcs | Description |
|-------|--------|-------|-------------|
| FNHighscore | ✅ | 5 | Single highscore entry (~0x54 bytes) | structs/data-classes.md |
| FNHighscoreList | ✅ | 10 | Sorted list\<FNHighscore\>, AddScore flow | structs/data-classes.md |
| BonusType | ✅ | 8 | Bonus category with child Bonus entries | structs/data-classes.md |
| LeaderboardList | ⏭️ | 11 | Online leaderboard data (defunct) |
| LeaderboardItem | ⏭️ | 6 | Single leaderboard entry (defunct) |

## Third-Party (TinyXML)

| Class | Status | Description |
|-------|--------|-------------|
| TiXmlDocument | ✅ | XML document root (used throughout) |
| TiXmlElement | ✅ | XML element (QueryIntAttribute, QueryFloatAttribute, Attribute) |
| TiXmlNode | ✅ | Tree navigation (FirstChildElement, NextSiblingElement) |
| Others (13 classes) | ⏭️ | Standard TinyXML — no RE needed |

## Summary

| Status | Count |
|--------|-------|
| ✅ Fully analyzed | 77 |
| 🔶 Partially analyzed | 17 |
| ❌ Not analyzed | 17 |
| ⏭️ Intentionally skipped | 18 |
| **Total** | **129** |
