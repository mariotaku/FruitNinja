# Original Source File Map

142 source files identified from `_GLOBAL__I_` C++ static initializer symbols. Each symbol corresponds to one `.cpp` compilation unit in the original FruitNinja Bada build. The address is the static initializer function.

Namespace column shows the C++ namespace from Ghidra symbol demangling. `Mortar::` = engine namespace. `—` = global namespace (no prefix in symbols).

## Engine Core (Mortar)

### Rendering
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| MatrixManager.cpp | 0x0019e5cc | — | Ported (simplified) |
| MatrixStack.cpp | 0x0019e940 | — | Ported (simplified) |
| DisplayManagerBada.cpp | 0x0019e13c | Mortar:: | Replaced by SDL Renderer |
| MortarCamera.cpp | 0x0019ebe4 | Mortar:: | Ported |
| Geometry.cpp | 0x001a0114 | Mortar:: | TODO |
| Geometry_Bada.cpp | 0x001a37f8 | Mortar:: | N/A (platform-specific) |
| Mesh.cpp | 0x001b14a8 | Mortar:: | Partial (MMD loader) |
| Mesh_Bada.cpp | 0x001941b4 | Mortar:: | N/A (platform-specific) |
| Model.cpp | 0x00193488 | Mortar:: | TODO |
| Font_Common.cpp | 0x00199d34 | Mortar:: | TODO |
| BakedString.cpp | 0x0019822c | — | TODO |
| Colour.cpp | 0x00183fec | — | Ported |
| Entity.cpp | 0x0019d944 | Mortar:: | Ported (base MortarEntity class) |
| DefaultIndexStreams_Bada.cpp | 0x001b7d2c | Mortar:: | N/A |
| DefaultVertexStreams_Bada.cpp | 0x001b8664 | Mortar:: | N/A |
| VertexElement.cpp | 0x001b7b28 | Mortar:: | TODO |
| NewsRenderer.cpp | 0x00191728 | — | Skip (online service) |

### Math & Collision
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| EngineMathBada.cpp | 0x001952bc | — | Ported (math3d) |
| ColAABB.cpp | 0x001b6608 | — | TODO |
| ColLine.cpp | 0x0019f984 | — | TODO |
| ColSphere.cpp | 0x0019ff08 | — | TODO |
| Collision.cpp | 0x001a0034 | — | TODO |

### Asset Management
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| MeshManager_Common.cpp | 0x00192a54 | — | TODO |
| MeshManager_PSP.cpp | 0x001a8684 | — | TODO |
| AnimationManager.cpp | 0x001925e4 | — | TODO (low priority) |
| Animation.cpp | 0x001ad628 | Mortar:: | TODO (low priority) |
| ResourceLoader.cpp | 0x001b4694 | Mortar:: | TODO |
| PathFunctions.cpp | 0x001b3b9c | — | Replaced by SDL paths |

### Input & Audio
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| InputManager.cpp | 0x00196d04 | — | Ported |
| InputDeviceBada.cpp | 0x00195c14 | — | Replaced by SDLInputTranslator |
| SoundManager_MAM.cpp | 0x0018cc30 | Mortar:: | TODO (SDL audio) |
| SystemManager.cpp | 0x0018aff0 | — | Stub |

### Threading & Debug
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| WorkGroup.cpp | 0x001a6fb8 | — | Skip (single-threaded port) |
| WorkerThread_Bada.cpp | 0x001888b0 | — | Skip |
| Profiler.cpp | 0x0011c7e8 | — | Skip |
| FPSCounter.cpp | 0x00138f78 | — | Skip |
| Precompiled.cpp | 0x0010cffc | — | N/A (build artifact) |

---

## Game Application

### Entry & Lifecycle
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| main.cpp | 0x0010d6b4 | — | Ported (main.cpp) |
| FruitNinja.cpp | 0x001826b4 | — | Ported (Game.cpp) |
| FruitNinjaEntry.cpp | 0x0018350c | — | Ported (Game.cpp) |
| Game.cpp | 0x0010a96c | — | Ported |
| Initialise.cpp | 0x0010ba14 | — | Ported (GameInitialise.cpp) |
| GameTask.cpp | 0x0016d0dc | — | Ported (GameTaskState.cpp) |
| SplashTask.cpp | 0x0016f7fc | — | Ported (SplashTask.cpp) |
| FrontendTask.cpp | 0x0016eccc | — | Ported (FrontendTask.cpp) |
| OptionsTask.cpp | 0x0016e568 | — | TODO |
| ParticleTask.cpp | 0x0016e884 | — | TODO |
| Save.cpp | 0x0012bf30 | — | TODO |

### Entities
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| Fruit.cpp | 0x0017a354 | — | Ported |
| Bomb.cpp | 0x0017200c | — | TODO |
| Slash.cpp | 0x0017e52c | — | TODO |
| Splat.cpp | 0x0017ff6c | — | TODO |
| SplatEffect.cpp | 0x00180530 | — | TODO |
| Coin.cpp | 0x00173d58 | — | TODO |
| EntityFactory.cpp | 0x00174294 | — | TODO |
| EntityTracker.cpp | 0x00174778 | — | TODO |
| FruitCamera.cpp | 0x00181870 | — | TODO |
| PSPParticles.cpp | 0x0011726c | — | TODO |
| ActorManager.cpp | 0x00170874 | Mortar:: | Ported |

### Managers & Systems
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| GameSound.cpp | 0x00129528 | — | TODO |
| ItemManager.cpp | 0x001134f8 | — | TODO |
| PowerUpManager.cpp | 0x00119df4 | — | TODO |
| BonusManager.cpp | 0x0010e9ec | — | TODO |
| Achievements.cpp | 0x00109728 | — | TODO |
| ComboChecker.cpp | 0x00110fc4 | — | TODO |
| HighscoreList.cpp | 0x001118f0 | — | TODO |
| LocalScoreList.cpp | 0x001145c4 | — | TODO |
| WaveManager.cpp | 0x00125f08 | — | TODO |
| TimeKeeper.cpp | 0x00128da8 | — | TODO |
| OperatorOptions.cpp | 0x00129864 | — | Skip (operator settings) |
| NetworkManager_common.cpp | 0x0018e3ec | — | Skip (online) |
| RegisterSocial.cpp | 0x0010d2a0 | — | Skip (online) |

---

## Screens

### Main Screens
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| MainScreen.cpp | 0x0014d934 | — | Ported |
| MainScreenArcade.cpp | 0x0014e108 | — | TODO |
| GameOverScreen.cpp | 0x00142ab8 | — | TODO |
| GameModeScreen.cpp | 0x0013fbc8 | — | TODO |
| PauseScreen.cpp | 0x00155698 | — | TODO |
| AboutScreen.cpp | 0x0012e700 | — | TODO |
| DojoScreen.cpp | 0x00137d80 | — | TODO |
| ShopScreen.cpp | 0x0015d7a0 | — | TODO |
| BonusScreen.cpp | 0x001336e8 | — | TODO |
| LeaderboardScreen.cpp | 0x001486d4 | — | TODO |
| OptionsScreen.cpp | 0x00153b80 | — | TODO |
| AttractScreen.cpp | 0x0012fe44 | — | TODO |
| BaseScreen.cpp | 0x00130694 | — | TODO |
| BladeScreen.cpp | 0x00131808 | — | TODO |
| PowerUpShop.cpp | 0x00156c84 | — | TODO |

### Online/Multiplayer Screens (skip for port)
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| UpsellScreen.cpp | 0x00163f34 | — | Skip |
| UpsellScreens.cpp | 0x00167450 | — | Skip |
| VSGameOverScreen.cpp | 0x00167bc8 | — | Skip (P2P multiplayer) |
| BuyStarfruitScreen.cpp | 0x001342f8 | — | Skip (IAP) |
| ChallengeHistoryScreenSL.cpp | 0x0013459c | — | Skip (SL = Social League) |
| ChallengeScreenSL.cpp | 0x00134840 | — | Skip |
| CreateChallengeScreenSL.cpp | 0x00137128 | — | Skip |
| LocalScoreEntryScreen.cpp | 0x0014a960 | — | TODO |

---

## UI Controls & Widgets

### Core HUD
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| HUDControl.cpp | 0x001445b0 | — | Ported |
| Hud.cpp | 0x00144e90 | — | Ported |
| GenericHUDControl.cpp | 0x00143c28 | — | TODO |
| MenuButton.cpp | 0x00150428 | — | Ported |
| MenuBackground.cpp | 0x0016f238 | — | TODO |
| CheckBox.cpp | 0x001350b4 | — | TODO |

### Input Controls
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| ComboBox.cpp | 0x00136694 | — | TODO |
| ListBox.cpp | 0x0014a5d4 | — | TODO |
| SliderControl.cpp | 0x00160268 (master ctor) / 0x001ea090 (vtable) | — | Defunct stub (no call sites) |
| KeyboardControl.cpp | 0x0014668c | — | TODO |
| ScrollingList.cpp | 0x0015beac | — | TODO |
| VerticalScroller.cpp | 0x0016880c | — | TODO |

### Game HUD Widgets
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| MissControl.cpp | 0x001522d0 | — | TODO |
| ComboControl.cpp | 0x00136e08 | — | TODO |
| ScoreControl.cpp | 0x00159998 | — | TODO |
| TimeControl.cpp | 0x00162b24 | — | TODO |
| SpeedControl.cpp | 0x001616d0 | — | TODO |
| BombCounter.cpp | 0x00131aac | — | TODO |
| CoinCounter.cpp | 0x001357e4 | — | TODO |
| StarfruitCounter.cpp | 0x00161b9c | — | TODO |
| TicketCounter.cpp | 0x00161e40 | — | TODO |
| CreditCounterControl.cpp | 0x001373cc | — | TODO |
| NotificationControl.cpp | 0x00153588 | — | TODO |
| OperatorAlertControl.cpp | 0x001538dc | — | Skip |
| ProgressionTimerControl.cpp | 0x00157e70 | — | TODO |
| ScreenFadeControl.cpp | 0x0015ab84 | — | TODO |
| ScoreMultiplyerBoard.cpp | 0x0015a384 | — | TODO |
| TutorialControl.cpp | 0x00163848 | — | TODO |
| MultiplayerTutorialControl.cpp | 0x001526d0 | — | Skip (multiplayer) |
| ZenVersusControl.cpp | 0x00168b80 | — | TODO |
| FruitFact.cpp | 0x0013a544 | — | TODO |
| FruitFactArcade.cpp | 0x0013da2c | — | TODO |
| FruitFactLite.cpp | 0x0013dcd0 | — | TODO |

---

## Modifiers & Effects
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| ScoreModifier.cpp | 0x0011cdb4 | — | TODO |
| SlashModifier.cpp | 0x0011f61c | — | TODO |
| TimeModifier.cpp | 0x001201b0 | — | TODO |
| WaveModifier.cpp | 0x00128448 | — | TODO |
| ScreenEffect.cpp | 0x0011e308 | — | TODO |
| TransitionFunctions.cpp | 0x001208cc | — | TODO |
| StringTableUtil.cpp | 0x0011fb78 | — | TODO |
| Utils.cpp | 0x00121054 | — | TODO |

---

## Multiplayer Packets (skip for port)
| Source File | Address | Namespace | Port Status |
|-------------|---------|-----------|-------------|
| FruitSlicedPacket.cpp | 0x0012cec8 | — | Skip |
| PacketFactory.cpp | 0x0012d1e8 | — | Skip |
| PointsPacket.cpp | 0x0012d624 | — | Skip |
| StartGamePacket.cpp | 0x0012da28 | — | Skip |
| WaveSyncPacket.cpp | 0x0012dea4 | — | Skip |
| LeaderboardList.cpp | 0x00147640 | — | Skip |
| LoadingJob.cpp | 0x0012e19c | — | TODO |

---

## Namespace Summary

Classes with confirmed `Mortar::` namespace (from Ghidra symbol demangling):

| Class | Source File | Category |
|-------|------------|----------|
| Mortar::DisplayManager / DisplayManagerBada | DisplayManagerBada.cpp | Rendering |
| Mortar::MortarCamera | MortarCamera.cpp | Rendering |
| Mortar::Geometry / Geometry_Bada | Geometry.cpp | Rendering |
| Mortar::Mesh / Mesh_Bada | Mesh.cpp | Rendering |
| Mortar::Model | Model.cpp | Rendering |
| Mortar::Font | Font_Common.cpp | Rendering |
| Mortar::VertexElement | VertexElement.cpp | Rendering |
| Mortar::Entity (MortarEntity) | Entity.cpp | Entity |
| Mortar::ActorManager | ActorManager.cpp | Entity |
| Mortar::Animation | Animation.cpp | Asset |
| Mortar::ResourceLoader | ResourceLoader.cpp | Asset |
| Mortar::SoundManager / SoundManagerMAM | SoundManager_MAM.cpp | Audio |
| Mortar::MortarSound | (in SoundManager_MAM.cpp) | Audio |
| Mortar::MortarGame | FruitNinja.cpp | App |
| Mortar::Touch | (no dedicated .cpp) | Input |
| Mortar::File | (no dedicated .cpp) | IO |
| Mortar::SmartPtr\<T\> | (template, no .cpp) | Util |
| Mortar::Delegate0-4\<T\> | (template, no .cpp) | Util |
| Mortar::IFileSystem / FileSystem_Direct | (in FileManager area) | IO |

Many engine classes do NOT use the `Mortar::` prefix in symbols (MatrixManager, SystemManager, InputManager, TextureManager, MeshManager, AnimationManager, FileManager, PSPParticleManager, Colour, BakedString, collision classes, etc.). The `Mortar::` prefix is inconsistent — address range (0x0018-0x001b) and functionality are more reliable classifiers.

---

## Port Progress Summary

| Category | Total | Ported | TODO | Skip | N/A |
|----------|-------|--------|------|------|-----|
| Engine Core | 37 | 8 | 13 | 8 | 8 |
| Game Application | 11 | 8 | 3 | 0 | 0 |
| Game Entities | 11 | 3 | 8 | 0 | 0 |
| Game Managers | 13 | 0 | 10 | 3 | 0 |
| Main Screens | 15 | 1 | 14 | 0 | 0 |
| Online Screens | 8 | 0 | 1 | 7 | 0 |
| UI Controls | 31 | 3 | 25 | 3 | 0 |
| Modifiers/Effects | 8 | 0 | 8 | 0 | 0 |
| Multiplayer Packets | 7 | 0 | 1 | 6 | 0 |
| **Total** | **142** | **23** | **83** | **27** | **8** |