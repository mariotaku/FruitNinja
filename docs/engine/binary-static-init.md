# Binary `_GLOBAL__I_*.cpp` Static-Init Inventory

This document enumerates every Itanium-ABI translation-unit static-init
function in `FruitNinja.exe` (one per .cpp file) and classifies what each
does. These run **before** `OspMain` / `main` via `.init_array` walk and
populate file-scope statics. Several of them are responsible for "magic
constants" the port may have inconsistently re-implemented or skipped.

Source: GhidraMCP `search_functions("_GLOBAL__I", ...)`, `read_memory` on
`.init_array` (0x001e826c-0x001e84a7), spot-check decompiles of every
distinct shape.

---

## 1. Method

1. **Symbol enumeration**: `search_functions(query="_GLOBAL__I",
   limit=500)` returned **142 hits**. Every symbol matches the form
   `_GLOBAL__I_<file>_cpp` (no `_GLOBAL__sub_I_*` variants present —
   GCC 4.4 emits the older non-sub form).

2. **Execution-order verification**: `.init_array` lives at
   `0x001e826c..0x001e84a7` (572 bytes = 143 entries × 4-byte ARM Thumb
   function pointers; LSB set = Thumb mode). Decoded entry [0] =
   `0x00109729 - 1 = 0x00109728` = `_GLOBAL__I_Achievements.cpp`,
   entry [1] = `0x0010A96D - 1 = 0x0010A96C` = `_GLOBAL__I_Game.cpp`,
   matching `search_functions` output. The 143rd entry that doesn't
   match a `_GLOBAL__I_*` symbol is the libstdc++ runtime
   `__static_initialization_and_destruction_0`-equivalent slot.

3. **Categorisation**: decompiled a representative sample of ~25
   functions covering every distinct shape, then matched the rest by
   name + size. The vast majority follow a single template caused by
   `Precompiled.h` — see Section 2.

---

## 2. The PCH boilerplate

GCC's static-init emits **per-TU** initialisers for every header-declared
file-scope static. Halfbrick's `Precompiled.h` declares a small set of
shared globals (with matching `.cpp`-side definitions in some "owner"
TU). Every TU that includes `Precompiled.h` therefore gets a
`_GLOBAL__I_<file>.cpp` containing **guarded copies** of the same init
sequence: each guard reads `*guard & 1`; the **first TU to run** wins
and performs the actual write, all later TUs no-op. This explains why
all ~140 init functions look nearly identical.

The shared globals (the "PCH boilerplate") are:

| Global  | Type     | Value          | Init pattern in decompile                                                                       |
| ------- | -------- | -------------- | ----------------------------------------------------------------------------------------------- |
| Identity matrix44 | Matrix44 (16 × float) | diagonal=1.0, off-diag=0.0 | `puVar[0]=puVar[5]=puVar[10]=puVar[15]=1.0f; rest = DAT_xxx (=0.0f)` |
| `Vec3::Zero` (a.k.a. `Vec3(0,0,0)`)   | Vector3 | (0, 0, 0)      | `_Vector3<float>::_Vector3(p_Var, DAT_xxx, DAT_xxx, DAT_xxx)` (DAT=0.0f) |
| `Vec3::One`         | Vector3 | (1, 1, 1)      | `_Vector3<float>::_Vector3(p_Var, 1.0, 1.0, 1.0)`                                              |
| `Vec2::Zero`        | Vector2 | (0, 0)         | `pfVar[0]=pfVar[1]=DAT_xxx (=0.0f)`                                                            |
| `Colour::Black` (or unnamed black)    | Colour (BGRA u8x4) | (0, 0, 0, 255) | `(undefined1*)[0..2]=0; [3]=0xff`                                                              |

Example: `_GLOBAL__I_Bomb_cpp` at `0x0017200c` — confirmed
`DAT_001722d8 = 0x00000000` (= 0.0f), `0x3f800000` constant = 1.0f. The
"identity matrix" decompile reads as a 16-float Matrix44 in row-major
order, with diagonal = 1.0 and rest = 0.0.

In addition, every TU emits per-class **`Mortar::__INIT_TYPE_ID()`**
guarded writes — one per type defined or used in that TU. These populate
RTTI-style class identity slots that `Mortar::ActorManager` /
`SmartPtr` / type-tag lookups depend on. Each call increments a global
counter and stores it. If they are skipped, type comparisons by ID
silently mis-fire.

---

## 3. Master table (142 entries)

Order column = position in `.init_array` (0-indexed).
Cat key:
- **PCH** = pure boilerplate (Identity + Vec3 Zero/One + Vec2/Colour + per-class TYPE_IDs only)
- **PCH+File** = boilerplate plus file-scope static ctors (strings, struct
  fields, pool ctors)
- **Singleton** = constructs a global singleton object
- **Strings** = constructs file-scope `Osp::Base::String` / `AsciiString`
- **TypeID-only** = single TYPE_ID write (no PCH, no other state)

| #   | Symbol                                  | Address      | Category    | What it sets (non-boilerplate items called out)                                       |
| --- | --------------------------------------- | ------------ | ----------- | ------------------------------------------------------------------------------------- |
| 0   | `_GLOBAL__I_Achievements.cpp`           | `0x00109728` | PCH+File    | + Black colour at `+0x158`                                                            |
| 1   | `_GLOBAL__I_Game.cpp`                   | `0x0010a96c` | PCH+File    | + `SmartPtr<Texture>`, `Mortar::StringTable` ctor, `Delegate1` ctor                   |
| 2   | `_GLOBAL__I_Initialise.cpp`             | `0x0010ba14` | PCH         |                                                                                       |
| 3   | `_GLOBAL__I_Precompiled.cpp`            | `0x0010cffc` | PCH         | (canonical owner of the PCH globals)                                                  |
| 4   | `_GLOBAL__I_RegisterSocial.cpp`         | `0x0010d2a0` | PCH         |                                                                                       |
| 5   | `_GLOBAL__I_main.cpp`                   | `0x0010d6b4` | PCH+File    | + Black colour                                                                        |
| 6   | `_GLOBAL__I_BonusManager.cpp`           | `0x0010e9ec` | PCH+File    | + Colour via `MakeColour_BGRA(0,0,0)`                                                 |
| 7   | `_GLOBAL__I_Achievements.cpp` (dup)     | `0x0010f5c5` | (init array entry, same shape)                                                        |
| 8   | `_GLOBAL__I_ComboChecker.cpp`           | `0x00110fc4` | PCH+File    | + Black colour at struct +0x60                                                        |
| 9   | `_GLOBAL__I_HighscoreList.cpp`          | `0x001118f0` | PCH+File    |                                                                                       |
| 10  | `_GLOBAL__I_ItemManager.cpp`            | `0x001134f8` | PCH+File    |                                                                                       |
| 11  | `_GLOBAL__I_LocalScoreList.cpp`         | `0x001145c4` | PCH+File    |                                                                                       |
| 12  | `_GLOBAL__I_PSPParticles.cpp`           | `0x0011726c` | PCH+File    | + black Colour at `+0xbf8`                                                            |
| 13  | `_GLOBAL__I_PowerUpManager.cpp`         | `0x00119df4` | PCH+File    |                                                                                       |
| 14  | `_GLOBAL__I_Profiler.cpp`               | `0x0011c7e8` | PCH         |                                                                                       |
| 15  | `_GLOBAL__I_ScoreModifier.cpp`          | `0x0011cdb4` | PCH+File    | + Black at struct +0x0                                                                |
| 16  | `_GLOBAL__I_ScreenEffect.cpp`           | `0x0011e308` | PCH+File    | + Black at struct +0x10                                                               |
| 17  | `_GLOBAL__I_SlashModifier.cpp`          | `0x0011f61c` | PCH+File    | + Black at struct +0x4                                                                |
| 18  | `_GLOBAL__I_StringTableUtil.cpp`        | `0x0011fb78` | PCH+File    | + Black at struct +0x4                                                                |
| 19  | `_GLOBAL__I_TimeModifier.cpp`           | `0x001201b0` | PCH+File    | + Black at struct +0x0                                                                |
| 20  | `_GLOBAL__I_TransitionFunctions.cpp`    | `0x001208cc` | PCH         |                                                                                       |
| 21  | `_GLOBAL__I_Utils.cpp`                  | `0x00121054` | PCH+File    | + Black at struct +0x0                                                                |
| 22  | `_GLOBAL__I_WaveManager.cpp`            | `0x00125f08` | PCH+File    | + Black at +0x47c, **Vec3(0,1,0)** axis-Y, **Vec3(1,0,0)** axis-X                     |
| 23  | `_GLOBAL__I_WaveModifier.cpp`           | `0x00128448` | PCH         |                                                                                       |
| 24  | `_GLOBAL__I_TimeKeeper.cpp`             | `0x00128da8` | PCH+File    | + Black at TimeKeeper, **`TimeKeeper::TimeKeeper(this)`** ctor on global              |
| 25  | `_GLOBAL__I_GameSound.cpp`              | `0x00129528` | PCH         |                                                                                       |
| 26  | `_GLOBAL__I_OperatorOptions.cpp`        | `0x00129864` | PCH         |                                                                                       |
| 27  | `_GLOBAL__I_Save.cpp`                   | `0x0012bf30` | PCH+File    | + Black at struct +0x28                                                               |
| 28  | `_GLOBAL__I_FruitSlicedPacket.cpp`      | `0x0012cec8` | PCH         |                                                                                       |
| 29  | `_GLOBAL__I_PacketFactory.cpp`          | `0x0012d1e8` | PCH         |                                                                                       |
| 30  | `_GLOBAL__I_PointsPacket.cpp`           | `0x0012d624` | PCH         |                                                                                       |
| 31  | `_GLOBAL__I_StartGamePacket.cpp`        | `0x0012da28` | PCH         |                                                                                       |
| 32  | `_GLOBAL__I_WaveSyncPacket.cpp`         | `0x0012dea4` | PCH         |                                                                                       |
| 33  | `_GLOBAL__I_LoadingJob.cpp`             | `0x0012e19c` | PCH         |                                                                                       |
| 34  | `_GLOBAL__I_AboutScreen.cpp`            | `0x0012e700` | PCH+File    |                                                                                       |
| 35  | `_GLOBAL__I_AttractScreen.cpp`          | `0x0012fe44` | PCH+File    |                                                                                       |
| 36  | `_GLOBAL__I_BaseScreen.cpp`             | `0x00130694` | PCH+File    |                                                                                       |
| 37  | `_GLOBAL__I_BladeScreen.cpp`            | `0x00131808` | PCH+File    |                                                                                       |
| 38  | `_GLOBAL__I_BombCounter.cpp`            | `0x00131aac` | PCH+File    |                                                                                       |
| 39  | `_GLOBAL__I_BonusScreen.cpp`            | `0x001336e8` | PCH+File    |                                                                                       |
| 40  | `_GLOBAL__I_BuyStarfruitScreen.cpp`     | `0x001342f8` | PCH+File    |                                                                                       |
| 41  | `_GLOBAL__I_ChallengeHistoryScreenSL.cpp` | `0x0013459c` | PCH+File    |                                                                                       |
| 42  | `_GLOBAL__I_ChallengeScreenSL.cpp`      | `0x00134840` | PCH+File    |                                                                                       |
| 43  | `_GLOBAL__I_CheckBox.cpp`               | `0x001350b4` | PCH+File    |                                                                                       |
| 44  | `_GLOBAL__I_CoinCounter.cpp`            | `0x001357e4` | PCH+File    |                                                                                       |
| 45  | `_GLOBAL__I_ComboBox.cpp`               | `0x00136694` | PCH+File    |                                                                                       |
| 46  | `_GLOBAL__I_ComboControl.cpp`           | `0x00136e08` | PCH+File    |                                                                                       |
| 47  | `_GLOBAL__I_CreateChallengeScreenSL.cpp`| `0x00137128` | PCH+File    |                                                                                       |
| 48  | `_GLOBAL__I_CreditCounterControl.cpp`   | `0x001373cc` | PCH+File    |                                                                                       |
| 49  | `_GLOBAL__I_DojoScreen.cpp`             | `0x00137d80` | PCH+File    |                                                                                       |
| 50  | `_GLOBAL__I_FPSCounter.cpp`             | `0x00138f78` | PCH+File    |                                                                                       |
| 51  | `_GLOBAL__I_FruitFact.cpp`              | `0x0013a544` | PCH+File    | + Black at +0x6c4, multiple `ZeroInitP_GameOverScreen` calls (TextureRef inits), Vec3(1,0,0), Vec3(0,1,0) |
| 52  | `_GLOBAL__I_FruitFactArcade.cpp`        | `0x0013da2c` | PCH         |                                                                                       |
| 53  | `_GLOBAL__I_FruitFactLite.cpp`          | `0x0013dcd0` | PCH         |                                                                                       |
| 54  | `_GLOBAL__I_GameModeScreen.cpp`         | `0x0013fbc8` | PCH+File    |                                                                                       |
| 55  | `_GLOBAL__I_GameOverScreen.cpp`         | `0x00142ab8` | PCH+File    |                                                                                       |
| 56  | `_GLOBAL__I_GenericHUDControl.cpp`      | `0x00143c28` | PCH+File    |                                                                                       |
| 57  | `_GLOBAL__I_HUDControl.cpp`             | `0x001445b0` | PCH+File    | + Black, Vec2(1,1)                                                                    |
| 58  | `_GLOBAL__I_Hud.cpp`                    | `0x00144e90` | PCH+File    |                                                                                       |
| 59  | `_GLOBAL__I_KeyboardControl.cpp`        | `0x0014668c` | PCH+File    |                                                                                       |
| 60  | `_GLOBAL__I_LeaderboardList.cpp`        | `0x00147640` | PCH+File    |                                                                                       |
| 61  | `_GLOBAL__I_LeaderboardScreen.cpp`      | `0x001486d4` | PCH+File    |                                                                                       |
| 62  | `_GLOBAL__I_ListBox.cpp`                | `0x0014a5d4` | PCH+File    |                                                                                       |
| 63  | `_GLOBAL__I_LocalScoreEntryScreen.cpp`  | `0x0014a960` | PCH+File    |                                                                                       |
| 64  | `_GLOBAL__I_MainScreen.cpp`             | `0x0014d934` | PCH+File    |                                                                                       |
| 65  | `_GLOBAL__I_MainScreenArcade.cpp`       | `0x0014e108` | PCH+File    |                                                                                       |
| 66  | `_GLOBAL__I_MenuButton.cpp`             | `0x00150428` | PCH+File    |                                                                                       |
| 67  | `_GLOBAL__I_MissControl.cpp`            | `0x001522d0` | PCH+File    |                                                                                       |
| 68  | `_GLOBAL__I_MultiplayerTutorialControl.cpp` | `0x001526d0` | PCH+File |                                                                                       |
| 69  | `_GLOBAL__I_NotificationControl.cpp`    | `0x00153588` | PCH+File    |                                                                                       |
| 70  | `_GLOBAL__I_OperatorAlertControl.cpp`   | `0x001538dc` | PCH+File    |                                                                                       |
| 71  | `_GLOBAL__I_OptionsScreen.cpp`          | `0x00153b80` | PCH+File    |                                                                                       |
| 72  | `_GLOBAL__I_PauseScreen.cpp`            | `0x00155698` | PCH+File    |                                                                                       |
| 73  | `_GLOBAL__I_PowerUpShop.cpp`            | `0x00156c84` | PCH+File    |                                                                                       |
| 74  | `_GLOBAL__I_ProgressionTimerControl.cpp`| `0x00157e70` | PCH+File    |                                                                                       |
| 75  | `_GLOBAL__I_ScoreControl.cpp`           | `0x00159998` | PCH+File    |                                                                                       |
| 76  | `_GLOBAL__I_ScoreMultiplyerBoard.cpp`   | `0x0015a384` | PCH+File    |                                                                                       |
| 77  | `_GLOBAL__I_ScreenFadeControl.cpp`      | `0x0015ab84` | PCH+File    |                                                                                       |
| 78  | `_GLOBAL__I_ScrollingList.cpp`          | `0x0015beac` | PCH+File    |                                                                                       |
| 79  | `_GLOBAL__I_ShopScreen.cpp`             | `0x0015d7a0` | PCH+File    |                                                                                       |
| 80  | `_GLOBAL__I_SliderControl.cpp`          | `0x00160934` | PCH+File    |                                                                                       |
| 81  | `_GLOBAL__I_SpeedControl.cpp`           | `0x001616d0` | PCH+File    |                                                                                       |
| 82  | `_GLOBAL__I_StarfruitCounter.cpp`       | `0x00161b9c` | PCH+File    |                                                                                       |
| 83  | `_GLOBAL__I_TicketCounter.cpp`          | `0x00161e40` | PCH+File    |                                                                                       |
| 84  | `_GLOBAL__I_TimeControl.cpp`            | `0x00162b24` | PCH+File    |                                                                                       |
| 85  | `_GLOBAL__I_TutorialControl.cpp`        | `0x00163848` | PCH+File    |                                                                                       |
| 86  | `_GLOBAL__I_UpsellScreen.cpp`           | `0x00163f34` | PCH+File    |                                                                                       |
| 87  | `_GLOBAL__I_UpsellScreens.cpp`          | `0x00167450` | PCH+File    |                                                                                       |
| 88  | `_GLOBAL__I_VSGameOverScreen.cpp`       | `0x00167bc8` | PCH+File    |                                                                                       |
| 89  | `_GLOBAL__I_VerticalScroller.cpp`       | `0x0016880c` | PCH+File    |                                                                                       |
| 90  | `_GLOBAL__I_ZenVersusControl.cpp`       | `0x00168b80` | PCH+File    |                                                                                       |
| 91  | `_GLOBAL__I_GameTask.cpp`               | `0x0016d0dc` | PCH+File    | **+ 7 fruit-spawn-default Vec3s at +0x68..+0xb0** (see Section 5.5), Black colour, 2× ZeroInit, Vec2(0,0), copy-ctor of two Vec3s |
| 92  | `_GLOBAL__I_OptionsTask.cpp`            | `0x0016e568` | PCH+File    |                                                                                       |
| 93  | `_GLOBAL__I_ParticleTask.cpp`           | `0x0016e884` | PCH+File    |                                                                                       |
| 94  | `_GLOBAL__I_FrontendTask.cpp`           | `0x0016eccc` | PCH+File    | + Black colour                                                                        |
| 95  | `_GLOBAL__I_MenuBackground.cpp`         | `0x0016f238` | PCH+File    |                                                                                       |
| 96  | `_GLOBAL__I_SplashTask.cpp`             | `0x0016f7fc` | PCH+File    | + Black colour at struct +0xc                                                         |
| 97  | `_GLOBAL__I_ActorManager.cpp`           | `0x00170874` | PCH         |                                                                                       |
| 98  | `_GLOBAL__I_Bomb.cpp`                   | `0x0017200c` | PCH+File    | + Vec2(0,0), Black, multiple `ZeroInitP_Coin` (texture refs)                          |
| 99  | `_GLOBAL__I_Coin.cpp`                   | `0x00173d58` | PCH+File    | + Black, `*puVar=0` raw zero on field                                                 |
| 100 | `_GLOBAL__I_EntityFactory.cpp`          | `0x00174294` | PCH         |                                                                                       |
| 101 | `_GLOBAL__I_EntityTracker.cpp`          | `0x00174778` | PCH+File    | **+ 3× `std::map<unsigned short, Entity*>::map()` ctors** (entity-id maps)            |
| 102 | `_GLOBAL__I_Fruit.cpp`                  | `0x0017a354` | PCH+File    | + Black, multiple `Mortar::SmartPtr<Texture2D>::SmartPtr` slots, multiple `ZeroInitPassthru_Fruit`, Vec3(1,0,0), Vec3(0,1,0), Colour copy-ctors |
| 103 | `_GLOBAL__I_Slash.cpp`                  | `0x0017e52c` | PCH+File    | + Vec2(0,0), Black, multiple `ZeroInit_SplatEntity`, **`Colour` array of 16** (palette), Colour copy-ctor (palette source), 12-element `SlashEntityGhost` array ctors |
| 104 | `_GLOBAL__I_Splat.cpp`                  | `0x0017ff6c` | PCH+File    | + Black, 2× `ZeroInit_FruitCamera`                                                    |
| 105 | `_GLOBAL__I_SplatEffect.cpp`            | `0x00180530` | PCH+File    |                                                                                       |
| 106 | `_GLOBAL__I_FruitCamera.cpp`            | `0x00181870` | PCH+File    | + Vec2(0,0), Black                                                                    |
| 107 | `_GLOBAL__I_FruitNinja.cpp`             | `0x001826b4` | **Strings** | **32× Osp::Base::String::String** (file path / locale-key constants for the app), then PCH boilerplate |
| 108 | `_GLOBAL__I_FruitNinjaEntry.cpp`        | `0x0018350c` | **Strings** | **32× Osp::Base::String::String** (Bada Application metadata strings)                 |
| 109 | `_GLOBAL__I_Colour.cpp`                 | `0x00183fec` | PCH         |                                                                                       |
| 110 | `_GLOBAL__I_WorkerThread_Bada.cpp`      | `0x001888b0` | PCH         |                                                                                       |
| 111 | `_GLOBAL__I_SystemManager.cpp`          | `0x0018aff0` | **Singleton** | **`SystemManager::SystemManager(this)`** — global SystemManager constructed         |
| 112 | `_GLOBAL__I_SoundManager_MAM.cpp`       | `0x0018cc30` | PCH+File    | **+ `std::_Rb_tree<u32, MAMSound*>::_Rb_tree()`** — sound table map ctor              |
| 113 | `_GLOBAL__I_NetworkManager_common.cpp`  | `0x0018e3ec` | PCH+File    | + `Mortar::OpenFeintNewsRenderer::OpenFeintNewsRenderer(this)` — global news renderer |
| 114 | `_GLOBAL__I_NewsRenderer.cpp`           | `0x00191728` | PCH+File    | + Vec2(0,0)                                                                           |
| 115 | `_GLOBAL__I_MeshManager_Common.cpp`     | `0x00192a54` | PCH (subset)| Vec3 Zero/One + 1 TYPE_ID (no Matrix/Vec2/Colour)                                     |
| 116 | `_GLOBAL__I_Model.cpp`                  | `0x00193488` | PCH+File    |                                                                                       |
| 117 | `_GLOBAL__I_Mesh_Bada.cpp`              | `0x001941b4` | PCH+File    |                                                                                       |
| 118 | `_GLOBAL__I_EngineMathBada.cpp`         | `0x001952bc` | **Singleton** | **`Math::Random::Random(this)`** — global PRNG seeded                                |
| 119 | `_GLOBAL__I_InputDeviceBada.cpp`        | `0x00195c14` | PCH+File    |                                                                                       |
| 120 | `_GLOBAL__I_InputManager.cpp`           | `0x00196d04` | PCH+File    |                                                                                       |
| 121 | `_GLOBAL__I_BakedString.cpp`            | `0x0019822c` | PCH (out-of-order: Vec2 first) |                                                                            |
| 122 | `_GLOBAL__I_Font_Common.cpp`            | `0x00199d34` | PCH+File    |                                                                                       |
| 123 | `_GLOBAL__I_Entity.cpp`                 | `0x0019d944` | PCH (subset)| Identity + Vec3 Zero only (no One, no Vec2, no Colour, no TYPE_IDs)                  |
| 124 | `_GLOBAL__I_DisplayManagerBada.cpp`     | `0x0019e13c` | PCH+File    | + raw `*ptr=0` clear at +DAT_0019e25c+4                                              |
| 125 | `_GLOBAL__I_MatrixManager.cpp`          | `0x0019e5cc` | PCH+File    |                                                                                       |
| 126 | `_GLOBAL__I_MatrixStack.cpp`            | `0x0019e940` | PCH+File    |                                                                                       |
| 127 | `_GLOBAL__I_MortarCamera.cpp`           | `0x0019ebe4` | PCH+File    |                                                                                       |
| 128 | `_GLOBAL__I_ColLine.cpp`                | `0x0019f984` | PCH+File    |                                                                                       |
| 129 | `_GLOBAL__I_ColSphere.cpp`              | `0x0019ff08` | PCH+File    |                                                                                       |
| 130 | `_GLOBAL__I_Collision.cpp`              | `0x001a0034` | PCH+File    |                                                                                       |
| 131 | `_GLOBAL__I_Geometry.cpp`               | `0x001a0114` | **TypeID-only** | single `__INIT_TYPE_ID()` write (1 type)                                          |
| 132 | `_GLOBAL__I_Geometry_Bada.cpp`          | `0x001a37f8` | **TypeID-only** | single `__INIT_TYPE_ID()` write                                                  |
| 133 | `_GLOBAL__I_WorkGroup.cpp`              | `0x001a6fb8` | **Singleton** | **`Mortar::ProcessorInfo::ProcessorInfo(this)`** — global worker-pool descriptor    |
| 134 | `_GLOBAL__I_MeshManager_PSP.cpp`        | `0x001a8684` | PCH (subset)| Vec3 Zero/One + 9 TYPE_IDs (no matrix, no Vec2, no Colour)                            |
| 135 | `_GLOBAL__I_Animation.cpp`              | `0x001ad628` | PCH (subset)| Vec3 Zero/One + 3 TYPE_IDs                                                            |
| 136 | `_GLOBAL__I_Mesh.cpp`                   | `0x001b14a8` | PCH (subset)| Vec3 Zero/One + Identity + 1 TYPE_ID                                                  |
| 137 | `_GLOBAL__I_PathFunctions.cpp`          | `0x001b3b9c` | **Strings** | **2× `Mortar::AsciiString::AsciiString` ctors** (file-path roots: `/Home/`, `/Res/` etc.) |
| 138 | `_GLOBAL__I_ResourceLoader.cpp`         | `0x001b4694` | **Singleton** | **`Mortar::CriticalSection::CriticalSection(this, name)`** — resource lock         |
| 139 | `_GLOBAL__I_ColAABB.cpp`                | `0x001b6608` | PCH (subset)| Identity + Vec3 Zero/One + 1 TYPE_ID                                                  |
| 140 | `_GLOBAL__I_VertexElement.cpp`          | `0x001b7b28` | PCH+File    |                                                                                       |
| 141 | `_GLOBAL__I_DefaultIndexStreams_Bada.cpp` | `0x001b7d2c` | PCH+File  |                                                                                       |
| 142 | `_GLOBAL__I_DefaultVertexStreams_Bada.cpp` | `0x001b8664` | **TypeID-only** | single `__INIT_TYPE_ID()` write                                                |

(Total: 142 + libstdc++ runtime entry = 143 init_array slots.)

---

## 4. Categorical summary

| Category       | Count | Notes                                                                                  |
| -------------- | ----- | -------------------------------------------------------------------------------------- |
| **PCH-only / PCH+File** with no novel state | ~118 | All redundant — the same global guards. The "first" winner runs, the rest no-op.       |
| **Singletons** | 4     | `SystemManager`, `Math::Random`, `ProcessorInfo`, `Mortar::CriticalSection` (resource lock) |
| **Singleton-on-task** | 1 | `TimeKeeper` (inside its PCH+File body)                                                |
| **Strings** (Osp::Base::String / AsciiString) | 3 | `FruitNinja.cpp` (32 strings), `FruitNinjaEntry.cpp` (32 strings), `PathFunctions.cpp` (2) |
| **TypeID-only** | 4 | `Geometry`, `Geometry_Bada`, `DefaultVertexStreams_Bada`, plus `MeshManager_Common` is subset |
| **File-scope ctor calls embedded in PCH+File body** | many | `SmartPtr<Texture>::SmartPtr` (Fruit/Bomb), `ZeroInitP_*` zero-init helpers, `std::map`/`std::_Rb_tree` ctors (`EntityTracker`, `SoundManager_MAM`), array-of-Colour palette ctor (`Slash`), array-of-`SlashEntityGhost` (`Slash`), `Mortar::StringTable::StringTable` (`Game`), `Mortar::Delegate1::BaseDelegate` (`Game`), `Mortar::OpenFeintNewsRenderer` (`NetworkManager_common`) |

---

## 5. Notable per-file findings

### 5.1 `_GLOBAL__I_FruitNinja.cpp` (`0x001826b4`)

Constructs **32 file-scope `Osp::Base::String`** objects from
`wchar_t*` literals at GOT-relative offsets `DAT_00182c38..DAT_00182cbc`
(strides of 4 = 32 strings). These are
`FruitNinjaApp`'s configuration / locale strings (path roots, key
prefixes for save data, scene-name keys, etc.). **Every string has a
matching `__aeabi_atexit` registration** so they get destructed at
program exit.

These 32 strings live at GOT offsets relative to `iVar4 = DAT_00182c30 +
0x1826c0`. Each is at struct-relative `+0x20, +0x34, +0x48, +0x5c, ...`
(stride 0x14 bytes = sizeof(`Osp::Base::String`)). The wchar_t source
literals are then PCH boilerplate.

The port has no equivalent — `FruitNinjaApp` constructs strings inline
in member init / functions on demand. **No fidelity issue** unless the
binary uses these as static globals shared across multiple call sites
(e.g. cached paths). Confirmed via xref on the `+0x20` slot — used in
`FruitNinjaApp::OnAppInitializing` (key resource paths). Port path
resolution is done by `ResolveAssetPath` which is functionally
equivalent.

### 5.2 `_GLOBAL__I_FruitNinjaEntry.cpp` (`0x0018350c`)

32 `Osp::Base::String` ctors at strides of 0x14 bytes from `iVar1 +
DAT_00183858`. These are the Bada `Application` framework's
**registration metadata** — package id, vendor, capability strings, etc.
**The port skips Bada framework — these are not needed.**

### 5.3 `_GLOBAL__I_PathFunctions.cpp` (`0x001b3b9c`)

Two `Mortar::AsciiString` ctors at `+DAT_001b3c00` and `+DAT_001b3c00
+ 0x28`. Likely the asset-path root prefixes (`"/Home/share/"` and
`"/Res/"` or similar). Port has its own `ResolveAssetPath` constants —
verify these match what the binary's `PathFunctions` emit.

### 5.4 `_GLOBAL__I_EngineMathBada.cpp` (`0x001952bc`)

Single line:
```c
Math::Random::Random(*(Random**)(GOT_BASE + DAT_001952d4));
```
Constructs the **global PRNG**. The `Math::Random` ctor in the binary
calls a system seed function (`time(NULL)` equivalent in Bada). If the
port's PRNG is constructed lazily on first use rather than at
program-start, the seed sequence may differ across the very first few
frames. Verify the port seeds at startup in `main()`/`SDL_main` before
any RNG consumer (e.g. `MainScreen` background random angles).

### 5.5 `_GLOBAL__I_GameTask.cpp` (`0x0016d0dc`) — **Magic spawn defaults**

Beyond the PCH boilerplate, this constructs **7 Vec3 spawn-parameter
defaults** at struct-relative offsets `+0x68..+0xb0`:

| Offset | Vec3 value                          | Resolved DAT       |
| ------ | ----------------------------------- | ------------------ |
| +0x68  | `(1.0, 1.0, 1.0)`                   | hardcoded literals |
| +0x74  | `(DAT_0016d3ec, DAT_0016d3f0, 1.0)` | `(1.7f, 0.3f, 1.0f)` |
| +0x80  | `(8.0, DAT_0016d3f4, 1.0)`          | `(8.0f, 0.1f, 1.0f)` |
| +0x8c  | `(20.0, DAT_0016d3f4, 1.0)`         | `(20.0f, 0.1f, 1.0f)` |
| +0x98  | `(4.0, DAT_0016d3f4, 1.0)`          | `(4.0f, 0.1f, 1.0f)` |
| +0xa4  | `(DAT_0016d3f4, DAT_0016d3f4, DAT_0016d3f4)` | `(0.1f, 0.1f, 0.1f)` |
| +0xb0  | `(DAT_0016d3f4, DAT_0016d3f4, DAT_0016d3f4)` | `(0.1f, 0.1f, 0.1f)` |

Constants resolved from `read_memory(0x0016d3e8, 16)`:
- `DAT_0016d3e8 = 0x00000000` → 0.0f
- `DAT_0016d3ec = 0x3FD9999A` → 1.7f (approximately)
- `DAT_0016d3f0 = 0x3E99999A` → 0.3f (approximately)
- `DAT_0016d3f4 = 0x3DCCCCCD` → 0.1f (approximately)

These look like **default fruit-spawn rate / variance parameters** (1
"base", a vec of (rate=8, variance=0.1) pair, etc.). The exact mapping
to GameTask field names isn't established here — implementer should
xref the `+0x68..+0xb0` field accesses inside `GameTask` member methods
to identify which physics/spawn parameters they back. (`GameTask`
struct layout in `docs/structs/`.) After
`+0xb0`, the body also copies `Vec3::Zero` into `+0xcc` and `+0xe4`,
calls `ZeroInitPassthru_GT` on `+0x10c`, and then does
`Colour::Colour(+0xf0, source)` — copy-ctor of a Colour from a static
source at `DAT_0016d43c` (likely a default UI tint).

### 5.6 `_GLOBAL__I_Slash.cpp` (`0x0017e52c`)

Notable beyond PCH:
- Allocates **a 16-element `Colour` array via a do/while loop**
  decrementing from 0xe to -2 (= 16 iterations) using
  `Colour::Colour(pCVar6); pCVar6 = pCVar6 + 1;` — this is the
  **default-initialised palette source**. There is then a
  `Colour::Colour(pCVar6, *(Colour**)(...))` call that copy-ctor's a
  *real* Colour into the head of the palette.
- Allocates **12-element `SlashEntityGhost` array** at struct +0x3c..0xbc
  via do/while of `SlashEntityGhost::SlashEntityGhost(this_00); this_00
  += 0x10;` (SlashEntityGhost size = 0x10 bytes per element, 12 ghosts).

The port has `g_Palette[16]` declared in `src/entities/SlashEntity.cpp`
with a hardcoded-literal initialiser — **verify the literals match the
binary's palette source `DAT_0017e828`**. This is a candidate "magic
constant" for fidelity audit.

### 5.7 `_GLOBAL__I_Fruit.cpp` (`0x0017a354`)

Multiple per-class init effects:
- Black `Colour` at `+0xcc`.
- 2× `SmartPtr<Texture2D>::SmartPtr` ctors at `+0x38, +0x3c` (a
  texture-ref pair).
- 3× `ZeroInitPassthru_Fruit` calls at `+0x98, +0x9c, +0x94` (8-byte
  zero blocks — ref-counted handles?).
- 2× `SmartPtr<Texture2D>::SmartPtr` at `+0x30, +0x34` (second
  texture-ref pair).
- 1× `ZeroInitPassthru_Fruit` at `+0xc0`.
- `Colour::Colour(*(Colour**)(...), copy)` — copy of source-Colour from
  `DAT_0017a674` into `*DAT_0017a670`.
- `Colour::Colour(*(Colour**)(...), 0x80, 0x80, 0xff, 0x80)` — RGBA
  `(128, 128, 255, 128)` constructed at `*DAT_0017a678`. **This is a
  fixed Colour constant the binary uses in Fruit rendering**;
  verify port has the same value. Likely this is `g_FruitOutlineTint`
  or similar.

### 5.8 `_GLOBAL__I_TimeKeeper.cpp` (`0x00128da8`)

Constructs the global `TimeKeeper` singleton via `TimeKeeper::TimeKeeper(this)`
on `*DAT_00129074`. The ctor zeroes the three `Timer` slots etc. Port
constructs equivalently in `TimeKeeper::GetInstance()` Meyers-singleton.

### 5.9 `_GLOBAL__I_SystemManager.cpp` (`0x0018aff0`)

Constructs the global `SystemManager` singleton via
`SystemManager::SystemManager(*DAT_0018b018)`. Same pattern as
TimeKeeper. Port has `SystemManager` per
`docs/engine/system-manager.md`.

### 5.10 `_GLOBAL__I_WorkGroup.cpp` (`0x001a6fb8`)

Constructs `Mortar::ProcessorInfo` at `*DAT_001a6fd0`. Holds CPU
topology / thread-pool sizing used by `WorkerThread_Bada`. Port runs
single-threaded and skips this — confirmed safe.

### 5.11 `_GLOBAL__I_ResourceLoader.cpp` (`0x001b4694`)

Constructs a `Mortar::CriticalSection` at `*DAT_001b46c4` with a name
literal at `DAT_001b46c0`. This guards the resource-loading queue. Port
uses a plain `std::mutex` or no synchronisation since loading is
synchronous in the SDL2 build.

### 5.12 `_GLOBAL__I_NetworkManager_common.cpp` (`0x0018e3ec`)

Constructs `Mortar::OpenFeintNewsRenderer` (the OpenFeint web-news
overlay) at `*DAT_0018e638`. Defunct online service — port can ignore.

### 5.13 `_GLOBAL__I_SoundManager_MAM.cpp` (`0x0018cc30`)

Constructs an empty `std::_Rb_tree<u32, MAMSound*>` at struct
`+0x34`. This is the sound-id → MAMSound lookup table backing
`SoundManager`. Port equivalent: `SoundManager`'s sound map field —
must be empty at construction.

### 5.14 `_GLOBAL__I_EntityTracker.cpp` (`0x00174778`)

Constructs **3 separate `std::map<u16, Entity*>`** instances back-to-back.
These are `EntityTracker`'s three internal maps (per ActorList? per
type-bucket?). Port should mirror this — three empty `std::map`
instances at startup. Port file `EntityTracker.cpp` should be checked
for equivalent default-initialised members.

### 5.15 `_GLOBAL__I_Game.cpp` (`0x0010a96c`)

Beyond PCH: `SmartPtr<Texture>::SmartPtr` at `*+0x17c`,
`Mortar::StringTable::StringTable(*+0x5b4)`, then a
`Mortar::Delegate1<int,int>::BaseDelegate` ctor wrapped with
`StackAllocatedPointer` and a vtable call at `(local_20+8)(this, ...)`
(installs the delegate into the Game singleton's delegate slot). The
port has `Game` constructed in `GameInitialise` not pre-`main`, but the
**StringTable should be empty and ready before `OnAppInitializing`**.

The static-init `Game` global lives in BSS — `GamePreInitialise`
zero-fills the whole 0x608 byte struct (per
`docs/engine/initialisation-asm-audit.md` already), so the port's
`Game` ctor runs with same effective state.

---

## 6. Port-impact assessment

The port should not blindly replicate every `_GLOBAL__I_*` — most are
PCH duplicates of `Vec3::Zero` / `Vec3::One` etc., already available
via `Vec3::Zero()` Meyers-singleton in
`src/engine/math/Vec3.h` line 74-75.

The candidates that **may be missing** in the port and need attention:

| Binary init feature                      | Port location             | Port status   | Risk                                                                      |
| ---------------------------------------- | ------------------------- | ------------- | ------------------------------------------------------------------------- |
| Identity Matrix44 global                 | `src/engine/math/Matrix44.h` | Likely fine — port computes identity inline; no static instance needed | LOW |
| `Vec3::Zero` / `Vec3::One` globals       | `src/engine/math/Vec3.h:74-75` | Meyers-singleton — OK | LOW |
| `Math::Random` global PRNG seeded at startup | `src/engine/math/?`     | Needs verification — does port seed before first `MainScreen` rand?  | **MEDIUM** — first-frame randomness fidelity |
| `SystemManager` singleton                | `src/engine/SystemManager.cpp` (per `docs/engine/system-manager.md`) | Implemented   | LOW |
| `TimeKeeper` singleton                   | `src/engine/TimeKeeper.cpp` | Implemented              | LOW |
| `Mortar::CriticalSection` resource lock  | (unported)                | Skipped — port single-threaded | LOW                                                                       |
| `ProcessorInfo` (WorkGroup)              | (unported)                | Skipped — port single-threaded | LOW                                                                       |
| `OpenFeintNewsRenderer`                  | (unported)                | Skipped — defunct service | LOW                                                                       |
| `EntityTracker`'s 3× `std::map<u16, Entity*>` | `src/engine/EntityTracker.cpp` | **Verify field count = 3** | MEDIUM — if port has fewer/more, behaviour diverges                       |
| `SoundManager_MAM`'s `std::_Rb_tree` map | `src/engine/SoundManager.cpp` | Implemented (uses `std::map`) | LOW |
| `Slash` 16-Colour palette, source from `DAT_0017e828` | `src/entities/SlashEntity.cpp:98 (g_Palette[16])` | **Verify port literals match binary's source palette**          | **MEDIUM** — palette colour fidelity                                                  |
| `Slash` 12× `SlashEntityGhost` array     | `src/entities/SlashEntity.cpp` | Implemented | LOW                                                                       |
| `Fruit` `Colour(0x80, 0x80, 0xff, 0x80)` constant at `*DAT_0017a678` | `src/entities/Fruit.cpp` | **Verify** — could be a tint/outline literal | MEDIUM — visual tint fidelity                                             |
| `Fruit` `Colour` copy from `DAT_0017a674` | `src/entities/Fruit.cpp` | **Identify and verify**     | MEDIUM                                                                    |
| `GameTask` 7 spawn-default Vec3s at `+0x68..+0xb0` | `src/engine/GameTask.cpp` (port likely doesn't expose these) | **Verify each parameter carries the binary's literal** | **HIGH** — gameplay-tuning constants                                                  |
| `GameTask` Colour at `+0xf0` from `DAT_0016d43c` | `src/engine/GameTask.cpp` | Verify          | LOW                                                                       |
| `WaveManager` Vec3(1, 0, 0) and Vec3(0, 1, 0) axes at file scope | `src/engine/WaveManager.cpp` | These are likely `Vec3::AxisX()` / `Vec3::AxisY()` — verify present | LOW |
| `FruitNinja.cpp` 32 path/locale `Osp::String`s | `src/FruitNinjaApp.cpp` | Probably replaced by `ResolveAssetPath` constants — **double-check no missing locale-key strings** | MEDIUM |
| `FruitNinjaEntry.cpp` 32 `Osp::String`s (Bada metadata) | (unported)        | Skipped — Bada framework | LOW                                                                       |
| `PathFunctions.cpp` 2 AsciiString file-path roots | `src/engine/?`            | **Verify port resolves the same root paths** | MEDIUM — asset paths                                                      |
| Per-class `Mortar::__INIT_TYPE_ID()` slots | (port uses different RTTI / typeid scheme — implicit) | Port equivalent: `dynamic_cast` or string-tag comparison | LOW unless port implements its own type-id system that needs seeding |

---

## 7. Recommended actions (top items)

Priority-ordered, for the implementer:

1. **GameTask 7 spawn-default Vec3s.** Decompile `GameTask` member
   functions that read fields at `+0x68..+0xb0` and identify what they
   represent (spawn-rate? variance? gravity? difficulty curve params?).
   Port: ensure `src/engine/GameTask.cpp` initialises those fields with
   `(1.0, 1.0, 1.0)`, `(1.7, 0.3, 1.0)`, `(8.0, 0.1, 1.0)`, `(20.0,
   0.1, 1.0)`, `(4.0, 0.1, 1.0)`, `(0.1, 0.1, 0.1)`, `(0.1, 0.1, 0.1)`
   in declaration / ctor / `GamePreInitialise` (or wherever the port
   constructs `GameTask`'s instance). Reference: `0x0016d3e8` data block
   at `0.0, 1.7, 0.3, 0.1` floats.

2. **Slash palette literal-vs-binary parity.** Compare `g_Palette[16]`
   in `src/entities/SlashEntity.cpp:98` against what the binary copies
   from `DAT_0017e828` source. Read 16 × `Colour` (16 × 4 bytes = 64
   bytes) at `*DAT_0017e828` and confirm the bytes match the port's
   array.

3. **`Math::Random` seeding moment.** Confirm the port seeds its global
   PRNG **before** any consumer runs (Splash, MainScreen first-frame
   render). Binary's order: `_GLOBAL__I_EngineMathBada.cpp` runs in
   `.init_array` slot ~118 (mid-init), then OspMain → app
   bootstrap → first frame. Port: should seed in
   `int main(...)` before SDL window/Init or in
   `FruitNinjaApp::OnAppInitializing`. **If lazy-seeded**, fidelity for
   first-frame visuals (random splash positions etc.) is lost.

4. **`Fruit` static `Colour(0x80,0x80,0xff,0x80)` at `*DAT_0017a678`.**
   Identify the field this writes to — read `*DAT_0017a678` to get the
   target struct/global address, then xref it to find consumers. Likely
   `g_FruitOutlineTint` or `g_FruitGlowTint`. Port should have the same
   value where it's used.

5. **`Fruit` copy-ctor Colour from `DAT_0017a674` into `*DAT_0017a670`.**
   Same drill as (4): read source bytes, find consumer. Likely a
   second tint constant.

6. **`PathFunctions.cpp` 2 AsciiStrings.** Read the C-string sources at
   `*DAT_001b3c04` and `*DAT_001b3c08` and confirm the port's
   `ResolveAssetPath` uses the same root prefixes.

7. **`EntityTracker` triple `std::map`.** Confirm
   `src/engine/EntityTracker.cpp` declares three `std::map<u16,
   Entity*>` members (or equivalent) — the binary's init constructs
   exactly three.

8. **`Game` `StringTable` and `Delegate1` slot.** Verify port's `Game`
   ctor constructs a `StringTable` at the equivalent struct offset and
   wires the `Delegate1` properly. Per
   `docs/engine/initialisation-asm-audit.md`, the Game struct is
   zero-filled by `GamePreInitialise`, so any non-zero default needs
   explicit assignment.

9. **`FruitNinja.cpp` 32 file-scope strings.** Diff against port's
   `FruitNinjaApp.cpp` constants — the implementer should verify no
   locale key / scene-name / save-key string in the binary is missing
   from the port's set.

10. **Per-class `__INIT_TYPE_ID` semantics.** Inspect a few
    `Mortar::__INIT_TYPE_ID()` consumers to determine whether the port
    relies on the resulting integer identifiers anywhere (likely no —
    port uses `typeid()` / `dynamic_cast` / string class names — but
    confirm via xref of one of the populated GOT slots like
    `*DAT_0017a784` in Fruit's init).

---

## 8. References

- Symbol enumeration: `mcp__GhidraMCP__search_functions(query="_GLOBAL__I", program="FruitNinja.exe", limit=500)` returned 142 results.
- `.init_array` segment: `0x001e826c..0x001e84a7` (572 bytes, 143 entries; entry 0 → `0x00109728`, entry 1 → `0x0010A96C`, …).
- Constants resolved:
  - `DAT_001722d8 = 0x00000000` (= 0.0f) — generic "zero literal" used by all `_Vector3<float>(zero)` constructions.
  - `DAT_0016d3e8/3ec/3f0/3f4 = 0.0/1.7/0.3/0.1` — GameTask spawn defaults.
- Existing related docs:
  - `docs/engine/initialisation-asm-audit.md` — `OspMain` / `GamePreInitialise` bootstrap (BSS zero-fill, runs between `_GLOBAL__I_*` chain and `OnAppInitializing`).
  - `docs/engine/system-manager.md` — `SystemManager` singleton.
  - `docs/engine/rng.md` — random-number internals (verify seeding moment).
  - `docs/structs/` — entity / GameTask struct layouts (resolve the GameTask `+0x68..+0xb0` field names).
