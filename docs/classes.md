# FruitNinja.exe Class Overview

**Format:** ELF (ARM32, Bada OS)
**Entry Point:** `OspMain`
**Engine:** Halfbrick Mortar Engine
**Classes Found:** ~70+

---

## 1. Application Classes (Fruit Ninja Game)

### Core Application

| Class | Inherits | Methods |
|-------|----------|---------|
| **FruitNinja** | Osp::App::Application | `CreateInstance()`, `OnAppInitializing()`, `OnAppTerminating()`, `OnForeground()`, `OnBackground()`, `OnTimerExpired()`, `Draw()`, `Cleanup()`, `InitEGL()`, `InitGL()` |
| **GlesForm** | Osp::Ui::Controls::Form | `OnDraw()`, `OnTouchPressed()`, `OnTouchMoved()`, `OnTouchReleased()` |
| **Game** | — | `Init()` (vtable-referenced) |

### Gameplay Logic

| Class | Methods |
|-------|---------|
| **Bomb** | `GetNumActiveForPlayer()`, `GetWait()` |
| **BombFlash** | `DrawUpdate()` (uses `Colour`, `Mortar::SmartPtr<Mortar::Texture>`) |
| **SlashEntity** | `CollideWithEntity()` |
| **SlashEntityGhost** | `Release()` |
| **SplatEntity** | `Destroy()` |
| **FruitCamera** | constructor |
| **FruitSlicedPacket** | constructor |
| **PowerUp** | `Deactivate()`, `Clone()` |
| **PowerUpManager** | `ActivatePower()`, `ActivateScreenEffect()` |
| **BonusManager** | `AddCombo()` |
| **WaveManager** | `BombScale()`, `SetupWaveQue()` |
| **WaveQue** | `AddWave()` |
| **WaveQueItem** | constructor |
| **PSPParticleEmitter** | `AddParticle()` |
| **PSPParticleManager** | constructor |
| **EffectImage** | constructor |

### HUD & UI

| Class | Inherits | Methods |
|-------|----------|---------|
| **HUD** | — | `BeginDraw()` |
| **HUDControl3d** | — | constructor |
| **ScoreControl** | — | `Reset()` |
| **ScreenFadeControl** | HUDControl3d | `CancelFade()` |
| **DojoScreen** | — | `AboutCallback()` |
| **GameOverScreen** | — | `CancelHUDProgressionTimer()`, `OnMultiplayerDisconnect()` |
| **GameModeScreen** | — | `SetIsChallenge()` |
| **AboutScreen** | — | constructor (takes `DojoScreen*`) |
| **ScrollingMenu** | — | `ClearTouch()`, `Collide()` |

### Scores & Achievements

| Class | Methods |
|-------|---------|
| **FNHighscore** | constructor |
| **FNHighscoreList** | `AddPlayerScore()` |
| **AchievementManager** | constructor |
| **LeaderboardManager** | `GetInstance()` (singleton) |

### Audio

| Class | Methods |
|-------|---------|
| **GameSound** | `FindFree()` |
| **BadaSound** | `MusicStop()`, `MusicPlay()`, `MusicResume()`, `MusicMute()` |
| **ASound** | constructor |
| **MAMAudioController** | `GetInstance()`, `Init()`, `StartAudioSubsystem()`, `StopAudioSubsystem()` |
| **MortarAudioMixerBada** | constructor |
| **MAMAudioThread** | `SubsystemBufferCopied()`, `ThreadMainLoop()` |

### Utility

| Class | Methods |
|-------|---------|
| **Colour** | `Lerp()` |
| **MatrixStack** | `Translate()`, `Scale()`, `RotZ()`, `Reset()` |
| **MatrixManager** | `SetupOrtho()` |
| **LinkedHeap** | `FreeListRemove()`, `FreeListShow()`, `BoundsCheck()` |
| **FileManager** | `FileSize()` |
| **ReferenceCounter** | (used by `Mortar::Job`) |

---

## 2. Mortar Engine Classes (Halfbrick)

### Core Engine

| Class | Namespace | Methods |
|-------|-----------|---------|
| **Entity** | Mortar | `Activate()` |
| **File** | Mortar | `IsOpen()`, `Close()`, `CanWrite()`, `Seek()`, `Write()` |
| **ResourceLoader** | Mortar | `BasePathSet()`, `BasePathGet()`, `Initialize()` |
| **DataReader** | Mortar | constructor |
| **AsciiString** | Mortar | `Append()`, `Compare()` |
| **Utf8StringProxy** | Mortar | `_Assign()` |
| **InputDevice** | Mortar | `AxisEvent()` |
| **Acceleration** | Mortar | `Clear()` |
| **Dialog** | Mortar | `AddButton()` |
| **WordWrap** | Mortar | `GetOption()` |

### Rendering

| Class | Namespace | Methods |
|-------|-----------|---------|
| **TextureManager** | Mortar | `Add()` |
| **Texture** | Mortar | (referenced via SmartPtr) |
| **Texture2DFromFile_Bada** | Mortar::Bada | `GetFormat()` |
| **Font** | Mortar | constructor |
| **Mesh** | Mortar | `BindSkeleton()`, `AddGeometry()`, `DrawQuad()`, `GetBoneLocalTransform()` |
| **Mesh::SharedPropsInfo** | Mortar::Mesh | `AddTextureMap()` |
| **Mesh::BoneBinding** | Mortar::Mesh | constructor |
| **Model** | Mortar | `SetEffectGroup()` |
| **Skeleton** | Mortar | — |
| **Skeleton::Bone** | Mortar::Skeleton | constructor |
| **Animation** | Mortar | constructor |
| **AnimationState** | Mortar | `LinkMesh()` |
| **GeometryBinding** | Mortar | constructor |
| **GeometryBinding_Bada** | Mortar | — |
| **GeometryBinding_Bada::PassBinding** | Mortar | `Apply()` |
| **EffectGroup** | Mortar | `EffectCount()` |
| **EffectProperty** | Mortar | constructor |
| **EffectPropertyDefinition** | Mortar | constructor |
| **EffectPropertyList** | Mortar | `Contains()` |
| **EffectPropertyValues** | Mortar | constructor |
| **Effect_Bada::Pass** | Mortar | constructor |

### Audio (Engine)

| Class | Namespace | Methods |
|-------|-----------|---------|
| **SoundManager** | Mortar | `GetMusicVolume()` |
| **MortarSound** | Mortar | constructor |

### Networking

| Class | Namespace | Methods |
|-------|-----------|---------|
| **NetworkManager** | Mortar | `SpawnThreadController()`, `GetNewsRenderer()`, `CancelNewsDisplay()`, `DownloadUserDataFromLeaderboard()`, `IsAnyPeerReadyForMultiplayer()` |
| **OpenFeintNewsRenderer** | Mortar | `CancelNewsRender()` |

### Threading

| Class | Namespace | Methods |
|-------|-----------|---------|
| **Job** | Mortar | `Run()` (inherits ReferenceCounter) |
| **WorkGroup** | Mortar | `AllocateThread()`, `WakeWorkerThread()` |
| **WorkerThread** | Mortar | constructor |

### Templates

| Template Class | Namespace | Methods |
|----------------|-----------|---------|
| **SmartPtr\<T\>** | Mortar | `operator=()` |
| **MicroBuffer\<char,32\>** | Mortar | `Allocate()`, `Resize()` |
| **LFQueue\<SmartPtr\<Job\>\>** | Mortar | `Push()` |
| **Event1\<T\>** | Mortar | `Register()`, `Trigger()` |
| **Delegate1\<void, int\>** | Mortar | — |
| **Delegate2\<void, P2PMessage, NetworkPacket*\>** | Mortar | `BaseDelegate()` |
| **Delegate3** | Mortar | — |
| **StackAllocatedPointer\<T\>** | Mortar | `Delete()` |
| **MemoryPool\<PSPParticleEmitter\>** | (global) | `Create()`, `Pop()` |
| **List\<MortarSound*\>** | (global) | `Begin()` |

---

## 3. Bada OS Framework Classes

| Class | Namespace | Used For |
|-------|-----------|----------|
| **Application** | Osp::App | App lifecycle |
| **Form** | Osp::Ui::Controls | GL rendering surface |
| **Control** | Osp::Ui | `GetPeerHandle()` |
| **Container** | Osp::Ui | `AddNotify()` |
| **Timer** | Osp::Base::Runtime | Game loop timer |
| **String** | Osp::Base | String handling |
| **ByteBuffer** | Osp::Base | Buffer I/O |
| **SettingInfo** | Osp::System | `GetValue()` |
| **PowerManager** | Osp::System | `KeepScreenOnState()` |
| **Player** | Osp::Media | `GetState()`, `Play()`, `SetMute()` |

---

## 4. TinyXML Classes

| Class | Inherits | Key Methods |
|-------|----------|-------------|
| **TiXmlBase** | — | base class |
| **TiXmlNode** | TiXmlBase | tree navigation |
| **TiXmlElement** | TiXmlNode | `Accept()` |
| **TiXmlDocument** | TiXmlNode | document root |
| **TiXmlText** | TiXmlNode | text content |
| **TiXmlComment** | TiXmlNode | XML comments |
| **TiXmlDeclaration** | TiXmlNode | `Print()` |
| **TiXmlUnknown** | TiXmlNode | unknown nodes |
| **TiXmlAttribute** | — | `Parse()` |
| **TiXmlAttributeSet** | — | `Find()`, `First()` |
| **TiXmlHandle** | — | convenience wrapper |
| **TiXmlVisitor** | — | visitor pattern base |
| **TiXmlPrinter** | TiXmlVisitor | `Visit()` |
| **TiXmlString** | — | `operator+=()`, `empty()` |
| **TiXmlCursor** | — | parse position |
| **TiXmlParsingData** | — | parse state |

---

## 5. Math Types

| Type | Notes |
|------|-------|
| **Math::Random** | RNG |
| **_Quaternion\<float\>** | Rotation |
| **_Vector3\<float\>** | 3D vector |
| **_Matrix44** | 4x4 matrix |

---

## Architecture Summary

```
OspMain (0x000d82a4) — anti-tamper hash check
  └─ OspMain_AppBootstrap (0x00183474) — argv → Application::Execute
       └─ FruitNinja (0x48 bytes, 3 bases: Application + IScreenEventListener + ITimerEventListener)
            ├─ OnAppInitializing (0x00182194) — EGL, GL, GlesForm, Timer, Audio, Game
            ├─ OnTimerExpired (0x0018269c) → Timer::Start(10) + FruitNinja::Draw (full game tick)
            ├─ GlesForm (OpenGL ES surface, touch input)
            ├─ Game (core game logic, 3-state machine: Splash → Game)
            │    ├─ GameInit (0x0016c644) — HUD, entities, screens, wave, sound
            │    ├─ GameUpdate (0x0016bed0) — main gameplay loop (359 lines)
            │    ├─ GameDraw (0x0016b888) — full render frame (211 lines)
            │    ├─ WaveManager / WaveQue (fruit/bomb spawning)
            │    ├─ SlashEntity / SlashEntityGhost (blade mechanics)
            │    ├─ Bomb / BombFlash (bomb logic)
            │    ├─ SplatEntity (splatter effects)
            │    ├─ PowerUpManager / PowerUp (power-ups)
            │    ├─ BonusManager / ScoreControl (scoring)
            │    ├─ PSPParticleManager / PSPParticleEmitter (particles)
            │    └─ FruitCamera (camera control)
            ├─ UI Screens
            │    ├─ MainScreen / DojoScreen (main menu)
            │    ├─ GameModeScreen (mode selection)
            │    ├─ GameOverScreen (results)
            │    ├─ PauseScreen (pause overlay)
            │    └─ AboutScreen (credits)
            ├─ HUD / ScreenFadeControl (overlay UI)
            ├─ Audio: BadaSound (0x874, 256 sounds, 8 active) → MAMAudioController → MAMAudioThread
            ├─ Networking: Mortar::NetworkManager (multiplayer, leaderboards — skipped for port)
            └─ Mortar Engine (rendering, resources, threading)
```
