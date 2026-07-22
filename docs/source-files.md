# Original Source File Map

142 source files identified from `_GLOBAL__I_` C++ static initializer symbols. Each symbol corresponds to one `.cpp` compilation unit in the original FruitNinja Bada build (v1.6.1). The address is the static initializer function.

**Purpose:** Cross-reference port file paths to binary symbol names and addresses for RE lookups. Namespace column shows the C++ namespace from Ghidra symbol demangling. `Mortar::` = engine namespace. `—` = global namespace (no prefix in symbols).

## Engine Core (Mortar)

### Rendering
| Source File | Address | Namespace |
|-------------|---------|-----------|
| MatrixManager.cpp | 0x0019e5cc | — |
| MatrixStack.cpp | 0x0019e940 | — |
| DisplayManagerBada.cpp | 0x0019e13c | Mortar:: |
| MortarCamera.cpp | 0x0019ebe4 | Mortar:: |
| Geometry.cpp | 0x001a0114 | Mortar:: |
| Geometry_Bada.cpp | 0x001a37f8 | Mortar:: |
| Mesh.cpp | 0x001b14a8 | Mortar:: |
| Mesh_Bada.cpp | 0x001941b4 | Mortar:: |
| Model.cpp | 0x00193488 | Mortar:: |
| Font_Common.cpp | 0x00199d34 | Mortar:: |
| BakedString.cpp | 0x0019822c | — |
| Colour.cpp | 0x00183fec | — |
| Entity.cpp | 0x0019d944 | Mortar:: |
| DefaultIndexStreams_Bada.cpp | 0x001b7d2c | Mortar:: |
| DefaultVertexStreams_Bada.cpp | 0x001b8664 | Mortar:: |
| VertexElement.cpp | 0x001b7b28 | Mortar:: |
| NewsRenderer.cpp | 0x00191728 | — |

### Math & Collision
| Source File | Address | Namespace |
|-------------|---------|-----------|
| EngineMathBada.cpp | 0x001952bc | — |
| ColAABB.cpp | 0x001b6608 | — |
| ColLine.cpp | 0x0019f984 | — |
| ColSphere.cpp | 0x0019ff08 | — |
| Collision.cpp | 0x001a0034 | — |

### Asset Management
| Source File | Address | Namespace |
|-------------|---------|-----------|
| MeshManager_Common.cpp | 0x00192a54 | — |
| MeshManager_PSP.cpp | 0x001a8684 | — |
| AnimationManager.cpp | 0x001925e4 | — |
| Animation.cpp | 0x001ad628 | Mortar:: |
| ResourceLoader.cpp | 0x001b4694 | Mortar:: |
| PathFunctions.cpp | 0x001b3b9c | — |

### Input & Audio
| Source File | Address | Namespace |
|-------------|---------|-----------|
| InputManager.cpp | 0x00196d04 | — |
| InputDeviceBada.cpp | 0x00195c14 | — |
| SoundManager_MAM.cpp | 0x0018cc30 | Mortar:: |
| SystemManager.cpp | 0x0018aff0 | — |

### Threading & Debug
| Source File | Address | Namespace |
|-------------|---------|-----------|
| WorkGroup.cpp | 0x001a6fb8 | — |
| WorkerThread_Bada.cpp | 0x001888b0 | — |
| Profiler.cpp | 0x0011c7e8 | — |
| FPSCounter.cpp | 0x00138f78 | — |
| Precompiled.cpp | 0x0010cffc | — |

---

## Game Application

### Entry & Lifecycle
| Source File | Address | Namespace |
|-------------|---------|-----------|
| main.cpp | 0x0010d6b4 | — |
| FruitNinja.cpp | 0x001826b4 | — |
| FruitNinjaEntry.cpp | 0x0018350c | — |
| Game.cpp | 0x0010a96c | — |
| Initialise.cpp | 0x0010ba14 | — |
| GameTask.cpp | 0x0016d0dc | — |
| SplashTask.cpp | 0x0016f7fc | — |
| FrontendTask.cpp | 0x0016eccc | — |
| OptionsTask.cpp | 0x0016e568 | — |
| ParticleTask.cpp | 0x0016e884 | — |
| Save.cpp | 0x0012bf30 | — |

### Entities
| Source File | Address | Namespace |
|-------------|---------|-----------|
| Fruit.cpp | 0x0017a354 | — |
| Bomb.cpp | 0x0017200c | — |
| Slash.cpp | 0x0017e52c | — |
| Splat.cpp | 0x0017ff6c | — |
| SplatEffect.cpp | 0x00180530 | — |
| Coin.cpp | 0x00173d58 | — |
| EntityFactory.cpp | 0x00174294 | — |
| EntityTracker.cpp | 0x00174778 | — |
| FruitCamera.cpp | 0x00181870 | — |
| PSPParticles.cpp | 0x0011726c | — |
| ActorManager.cpp | 0x00170874 | Mortar:: |

### Managers & Systems
| Source File | Address | Namespace |
|-------------|---------|-----------|
| GameSound.cpp | 0x00129528 | — |
| ItemManager.cpp | 0x001134f8 | — |
| PowerUpManager.cpp | 0x00119df4 | — |
| BonusManager.cpp | 0x0010e9ec | — |
| Achievements.cpp | 0x00109728 | — |
| ComboChecker.cpp | 0x00110fc4 | — |
| HighscoreList.cpp | 0x001118f0 | — |
| LocalScoreList.cpp | 0x001145c4 | — |
| WaveManager.cpp | 0x00125f08 | — |
| TimeKeeper.cpp | 0x00128da8 | — |
| OperatorOptions.cpp | 0x00129864 | — |
| NetworkManager_common.cpp | 0x0018e3ec | — |
| RegisterSocial.cpp | 0x0010d2a0 | — |

---

## Screens

### Main Screens
| Source File | Address | Namespace |
|-------------|---------|-----------|
| MainScreen.cpp | 0x0014d934 | — |
| MainScreenArcade.cpp | 0x0014e108 | — |
| GameOverScreen.cpp | 0x00142ab8 | — |
| GameModeScreen.cpp | 0x0013fbc8 | — |
| PauseScreen.cpp | 0x00155698 | — |
| AboutScreen.cpp | 0x0012e700 | — |
| DojoScreen.cpp | 0x00137d80 | — |
| ShopScreen.cpp | 0x0015d7a0 | — |
| BonusScreen.cpp | 0x001336e8 | — |
| LeaderboardScreen.cpp | 0x001486d4 | — |
| OptionsScreen.cpp | 0x00153b80 | — |
| AttractScreen.cpp | 0x0012fe44 | — |
| BaseScreen.cpp | 0x00130694 | — |
| BladeScreen.cpp | 0x00131808 | — |
| PowerUpShop.cpp | 0x00156c84 | — |

### Online/Multiplayer Screens (defunct)
| Source File | Address | Namespace |
|-------------|---------|-----------|
| UpsellScreen.cpp | 0x00163f34 | — |
| UpsellScreens.cpp | 0x00167450 | — |
| VSGameOverScreen.cpp | 0x00167bc8 | — |
| BuyStarfruitScreen.cpp | 0x001342f8 | — |
| ChallengeHistoryScreenSL.cpp | 0x0013459c | — |
| ChallengeScreenSL.cpp | 0x00134840 | — |
| CreateChallengeScreenSL.cpp | 0x00137128 | — |
| LocalScoreEntryScreen.cpp | 0x0014a960 | — |

---

## UI Controls & Widgets

### Core HUD
| Source File | Address | Namespace |
|-------------|---------|-----------|
| HUDControl.cpp | 0x001445b0 | — |
| Hud.cpp | 0x00144e90 | — |
| GenericHUDControl.cpp | 0x00143c28 | — |
| MenuButton.cpp | 0x00150428 | — |
| MenuBackground.cpp | 0x0016f238 | — |
| CheckBox.cpp | 0x001350b4 | — |

### Input Controls
| Source File | Address | Namespace |
|-------------|---------|-----------|
| ComboBox.cpp | 0x00136694 | — |
| ListBox.cpp | 0x0014a5d4 | — |
| SliderControl.cpp | 0x00160268 (master ctor) / 0x001ea090 (vtable) | — |
| KeyboardControl.cpp | 0x0014668c | — |
| ScrollingList.cpp | 0x0015beac | — |
| VerticalScroller.cpp | 0x0016880c | — |

### Game HUD Widgets
| Source File | Address | Namespace |
|-------------|---------|-----------|
| MissControl.cpp | 0x001522d0 | — |
| ComboControl.cpp | 0x00136e08 | — |
| ScoreControl.cpp | 0x00159998 | — |
| TimeControl.cpp | 0x00162b24 | — |
| SpeedControl.cpp | 0x001616d0 | — |
| BombCounter.cpp | 0x00131aac | — |
| CoinCounter.cpp | 0x001357e4 | — |
| StarfruitCounter.cpp | 0x00161b9c | — |
| TicketCounter.cpp | 0x00161e40 | — |
| CreditCounterControl.cpp | 0x001373cc | — |
| NotificationControl.cpp | 0x00153588 | — |
| OperatorAlertControl.cpp | 0x001538dc | — |
| ProgressionTimerControl.cpp | 0x00157e70 | — |
| ScreenFadeControl.cpp | 0x0015ab84 | — |
| ScoreMultiplyerBoard.cpp | 0x0015a384 | — |
| TutorialControl.cpp | 0x00163848 | — |
| MultiplayerTutorialControl.cpp | 0x001526d0 | — |
| ZenVersusControl.cpp | 0x00168b80 | — |
| FruitFact.cpp | 0x0013a544 | — |
| FruitFactArcade.cpp | 0x0013da2c | — |
| FruitFactLite.cpp | 0x0013dcd0 | — |

---

## Modifiers & Effects
| Source File | Address | Namespace |
|-------------|---------|-----------|
| ScoreModifier.cpp | 0x0011cdb4 | — |
| SlashModifier.cpp | 0x0011f61c | — |
| TimeModifier.cpp | 0x001201b0 | — |
| WaveModifier.cpp | 0x00128448 | — |
| ScreenEffect.cpp | 0x0011e308 | — |
| TransitionFunctions.cpp | 0x001208cc | — |
| StringTableUtil.cpp | 0x0011fb78 | — |
| Utils.cpp | 0x00121054 | — |

---

## Multiplayer Packets (defunct)
| Source File | Address | Namespace |
|-------------|---------|-----------|
| FruitSlicedPacket.cpp | 0x0012cec8 | — |
| PacketFactory.cpp | 0x0012d1e8 | — |
| PointsPacket.cpp | 0x0012d624 | — |
| StartGamePacket.cpp | 0x0012da28 | — |
| WaveSyncPacket.cpp | 0x0012dea4 | — |
| LeaderboardList.cpp | 0x00147640 | — |
| LoadingJob.cpp | 0x0012e19c | — |

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

