# MainScreen

## MainScreen : HUDControl3d (size = 0x120)

The main menu / dojo screen. Contains all menu buttons, toggle buttons (sound/music), textures, and a font.

### Struct Layout

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00–0x7b | HUDControl3d | super | Base class (pos, size, rotation, texture, vtable) |
| +0x7c | float | m_OrigSizeX | Copy of size_x |
| +0x80 | float | m_OrigSizeY | Copy of size_y |
| +0x84 | float | m_OrigSizeZ | Copy of size_z |
| +0x88 | SmartPtr\<Texture\> | m_TexNewGame | `newgame.tex` |
| +0x8c | SmartPtr\<Texture\> | m_TexDojoIcon | `dojo_icon.tex` |
| +0x90 | SmartPtr\<Texture\> | m_TexQuit | `quit.tex` |
| +0x94 | SmartPtr\<Texture\> | m_TexOpenFeint | `openfeint.tex` |
| +0x98 | SmartPtr\<Texture\> | m_TexMoreGames | `more_games.tex` |
| +0x9c | MenuButton* | pPlayButton | Created lazily in state 0→1 |
| +0xa0 | MenuButton* | pDojoButton | Created lazily in state 0→1 |
| +0xa4 | MenuButton* | pLeaderboardBtn | Created lazily in state 1 |
| +0xa8 | MenuButton* | pMoreGamesBtn | Created lazily in state 1 |
| +0xac | MenuButton* | pSoundToggle | Created in state 0 |
| +0xb0 | MenuButton* | pMusicToggle | Created in state 0 |
| +0xb4 | SmartPtr\<Texture\> | m_TexCommingSoon | `comming_soon.tex` |
| +0xc4 | SmartPtr\<Texture\> | m_TexSoundOn | `sound.tex` |
| +0xc8 | SmartPtr\<Texture\> | m_TexSoundOff | `sound_cross.tex` |
| +0xcc | SmartPtr\<Texture\> | m_TexMusicOn | `music.tex` |
| +0xd0 | SmartPtr\<Texture\> | m_TexMusicOff | `music_cross.tex` |
| +0xd4 | SmartPtr\<Model\> | m_Model | (null — set to null in ctor) |
| +0xd8 | SmartPtr\<Texture\> | m_TexSliceFruit | `slice_fruit.tex` — dojo decoration behind logo |
| +0xdc | Vec3 | m_LogoFruitPos | "FRUIT" text position (set in UpdateScreenElements) |
| +0xe8 | float | m_Alpha | = 1.0 (lerps toward global alpha target) |
| +0xec | Vec3 | m_LogoFruitTextPos | fruit_text.tex draw position (Draw uses +0xec for fruit_text) |
| +0xf4 | float | field_0xf4 | = 0.0 |
| +0xf8 | Vec3 | m_LogoNinjaTextPos | ninja_text.tex draw position (Draw uses +0xf8 for ninja_text) |
| +0xfc | float | m_WindowCenter | = windowHeight/2 + 160.0 |
| +0x100 | float | field_0x100 | = 0.0 |
| +0x104 | float | m_BounceVelocity | Bounce velocity for logo (decays) |
| +0x108 | float | m_field108 | = 0.0 (accumulator for state 0x13/0x14) |
| +0x10c | int | m_State | State machine variable |
| +0x110 | float | m_Timer | Transition countdown (init 0.0) |
| +0x114 | SmartPtr\<Texture\> | m_TexGCAchievements | `gc_achievements.tex` |
| +0x118 | float | m_Timer2 | Second timer (various uses per state) |
| +0x11c | Font* | m_pFont | `fonts/verdana.fnt` |

### Global Textures (not on struct)

Loaded in ctor, assigned to global SmartPtr via GOT:
- `blurry_backing.tex` — fullscreen blur overlay (used as background tri-list)
- `fruit_text.tex` — logo "FRUIT" text
- `ninja_text.tex` — logo "NINJA" text

---

## State Enum

```cpp
enum MainScreenState {
    STATE_CAMERA_ZOOM      = 0,    // Camera zoom-in from splash, create toggles + play/shop
    STATE_CREATE_BUTTONS   = 1,    // Create leaderboard/moregames buttons, active menu
    STATE_GAME_START       = 2,    // Direct game start, camera fade
    STATE_DOJO_WAIT_A      = 3,    // Wait for entities → DojoScreen
    STATE_DOJO_WAIT_B      = 4,    // Wait for entities → DojoScreen (about)
    STATE_SLIDE_IN         = 8,    // Slide-in return transition
    STATE_LEADERBOARD      = 9,    // Network: GameCenter dashboard (skip)
    STATE_MORE_GAMES       = 10,   // Network: more games (skip)
    STATE_NEWS             = 0x0b, // Network: news update (skip)
    STATE_MODE_SELECT      = 0x0e, // Slide-out → GameModeScreen
    STATE_MODE_SELECT_2    = 0x0f, // Slide-out continued
    STATE_MATCHMAKER       = 0x10, // Network: matchmaker (skip)
    STATE_CAMERA_FADE      = 0x11, // Camera fade after game return
    STATE_LOADING_A        = 0x13, // Timer accumulate + loading symbol
    STATE_LOADING_B        = 0x14, // Timer accumulate + loading symbol
    STATE_DOJO_WAIT_C      = 0x15, // Wait for entities (variant)
    STATE_DOJO_WAIT_D      = 0x16, // Wait for entities (variant)
    STATE_QUIT_WAIT        = 0x17, // Tutorial reset → bomb transition
    STATE_QUIT_BOMB        = 0x18, // BombFlash → SystemManager::QuitGame
};
```

## Constructor (0x0014c430, 159 lines)

```
1. Call HUDControl3d::HUDControl3d()
2. Init 13 SmartPtr<Texture> fields (ctor + SetNull for toggle pairs)
3. Load globals: blurry_backing.tex, fruit_text.tex, ninja_text.tex
4. Load background: slice_fruit.tex → m_TexBg
5. Set model to null (SmartPtrNull_Model)
6. Load button textures: newgame, dojo_icon, gc_achievements, more_games, quit, openfeint
7. Load font: fonts/verdana.fnt (size 0x438)
8. SetNull on toggle textures, then load: sound.tex, music_cross.tex, sound_cross.tex, music.tex
9. Load logo: comming_soon.tex → m_TexCommingSoon
10. Set size = (480.0, 138.0, 1.0)
11. Set position = (0.0, (320.0 - size_y) * 0.5, 0.0)
12. m_WindowCenter = windowHeight/2 + 160.0
13. Zero all button pointers, state=0, timers=0
```

---

## Draw (0x0014d4ec, 171 lines)

Renders the main menu background, logo textures, and optional loading symbol.

```
// Skip drawing for certain states
if state == 0x11 || state == 0x0d: return
if state in {3,4,0x15,0x16} && timer2 == 0.0: return

// 1. Background tri-list (blurry_backing.tex)
if (!initialized):
    Build 2 QUADCUSTOMVERTEX triangles (6 verts) at game+0x6cc:
        colour = Colour(0, 0, 0, 0x80)  // semi-transparent black
        UV coords: (0.5, 0.5) / (1.0, 1.0)
        positions: corners at (-1.0, -1.0, -0.6875) to (1.0, 1.0, 3.5)
    initialized = true

Texture::Set(blurry_backing_tex)
ResetMatrix → Scale(size) → Translate(pos) → Upload
Mesh::DrawTriList(game+0x6cc, 3 triangles, culling=true)
Texture::UnSet

// 2. "FRUIT" text logo (fruit_text.tex) — inside same block as shade
    Texture::Set(fruit_text_tex)
    ResetMatrix
    Scale to texture dimensions (width, height, 0.0)
    Translate to m_LogoFruitTextPos (+0xec)
    Upload → TintColour → DrawQuad → UnSet

// 3. "NINJA" text logo (ninja_text.tex)
if ninja_text_tex is valid:
    same pattern, translate to m_LogoNinjaTextPos (+0xf8)

// 4. Dojo decoration (slice_fruit.tex / m_TexSliceFruit)
if m_TexSliceFruit is valid:
    Scale to texture dimensions
    Translate to m_LogoFruitPos (+0xdc)
    DrawQuad

// 5. Loading symbol (states 0x13, 0x14 only)
if state == 0x13 or 0x14:
    DrawLoadingSymbol(this, param_1)

// 6. "Coming soon" / Logo overlay (comming_soon.tex)
if m_TexCommingSoon valid AND pPlayButton exists:
    Scale to (0.5, aspectRatio * 0.5, 1.0)
    Translate to (DAT, 7.0, 0.0)
    DrawQuad
```

**Draw order (back to front):**
1. Semi-transparent black overlay (blurry_backing)
2. "FRUIT" text
3. "NINJA" text
4. Dojo decoration (slice_fruit)
5. Loading symbol (if applicable)
6. Logo overlay (comming_soon)

Buttons are drawn separately by HUD::Draw (they're added as HUDControls).

---

## Update (0x0014b278, 677 lines) — State Machine

### State Table

| State | Enum | Purpose | Transition |
|-------|------|---------|------------|
| **0** | `CAMERA_ZOOM` | Camera zoom-in from splash. Creates sound/music toggles, Play + Dojo buttons. Lerps camera to -1.0 (rate 0.125). | → `CREATE_BUTTONS` |
| **1** | `CREATE_BUTTONS` | Creates Leaderboard/MoreGames buttons. Checks `ItemManager::AreNewItems()` for "new" badge. Active menu. | (stays) |
| **2** | `GAME_START` | Direct game start. Camera > 0.999: reset WaveManager. Decays camera ×0.75. | → `CAMERA_FADE` |
| **3, 4** | `DOJO_WAIT_A/B` | Wait for `ActorManager::GetNumEntities() == 0`, decay timer2 ×0.75. Create `DojoScreen`. | → (screen) |
| **8** | `SLIDE_IN` | Slide-in return. Lerps timer2 → 1.0, then accumulates dt. After 1.5s reset. | → `CAMERA_ZOOM` |
| **9, 10** | `LEADERBOARD/MORE_GAMES` | Network/GameCenter (skip for port). | → `CAMERA_ZOOM` |
| **0xb** | `NEWS` | Network news update (skip for port). | → `CREATE_BUTTONS` |
| **0xe, 0xf** | `MODE_SELECT/_2` | Slide-out: decay timer2 ×0.85. At 0.25 threshold: create `GameModeScreen`. | → (screen) |
| **0x10** | `MATCHMAKER` | Open matchmaker (skip for port). | → `CAMERA_ZOOM` |
| **0x11** | `CAMERA_FADE` | Camera fade after game return. Decays camera ×0.75 until settled. | (stays) |
| **0x13, 0x14** | `LOADING_A/B` | Accumulate field108 += dt×8. When ≥ 8.0 → reset. | → `CAMERA_ZOOM` |
| **0x15, 0x16** | `DOJO_WAIT_C/D` | Wait for entities variant (same as 3/4 logic). | → (screen) |
| **0x17** | `QUIT_WAIT` | Reset tutorial. Wait for entities. If field +0x4c == 2: HitMenuBomb. | → `QUIT_BOMB` or `CAMERA_ZOOM` |
| **0x18** | `QUIT_BOMB` | BombFlash → `SystemManager::QuitGame()`. | → (exit) |

### Position Update (end of Update, all states)

```
// Sound/music toggle texture swap
pMusicToggle->texture = game->musicEnabled ^ 1 ? m_TexMusicOn : m_TexMusicOff
pSoundToggle->texture = game->soundEnabled ^ 1 ? m_TexSoundOn : m_TexSoundOff

// Button Y-position (135.5 base, slides off with camera transition)
if both toggles exist:
    pSoundToggle->pos.y = 135.5
    pMusicToggle->pos.y = 135.5
    if cameraTransition <= 0:
        pSoundToggle->pos.x = 20.0   // on-screen
        pMusicToggle->pos.x = -20.0
    else:
        pSoundToggle->pos.x = 216.0  // normal positions
        pMusicToggle->pos.x = 176.0

    // Slide offset based on pause amount
    pauseAmount = clamp(cameraTransition + GetPauseAmount(), 0, 1)
    slideOffset = size_y * 2 * (1 - pauseAmount)
    pSoundToggle->active = pMusicToggle->active = (pauseAmount > 0.01)
    pSoundToggle->pos.y += slideOffset
    pMusicToggle->pos.y += slideOffset

    // Same-screen multiplayer: negate positions when player 2 paused
    if IsSameScreenMultiplayer() && camera > -0.01 && GetPausedBy() == 1:
        negate both button positions
```

---

## Hide (0x0014ad04, 7 lines)

```cpp
void MainScreen::Hide() {
    m_State = 0x11;
    pos = Vec3(0.0, 0.0, 0.0);  // DATs resolve to 0.0
}
```

---

## UpdateScreenElements (0x0014ad3c, ~60 lines)

Called with `(param_1=cameraTransition, param_2=time)`. Handles the bouncing logo animation.

```
// Clamp minimum param1
if param1 < threshold: param1 = threshold

// Logo positions (fruit + ninja text)
m_LogoFruitTextPos = (0.0, ?, 0.0)
m_LogoNinjaTextPos = (same as NinjaPos initially)

// Bounce physics
m_BounceVelocity += param1 * decayRate
m_WindowCenter += m_BounceVelocity * param1 * 15.0

// Bounce at floor
floorY = pos.y + 18.0
m_LogoFruitPos.y = floorY
m_LogoNinjaTextPos = m_LogoFruitTextPos  // copy

if m_WindowCenter < floorY - 15.0:
    m_WindowCenter = floorY - 15.0
    m_BounceVelocity *= -0.25        // bounce with energy loss
    if abs(velocity) < 3.0 && param2 > threshold && param1 > 0:
        m_BounceVelocity = 0.0       // settle
        globalAlphaTarget = 0.0

// Alpha lerp
m_Alpha += (globalAlphaTarget - m_Alpha) * 0.25

// Final logo offset (shift down and left)
m_LogoNinjaTextPos += Vec3(0.0, -17.0, 0.0) * 2.0
```

---

## DeleteMenuButtons (0x0014aee8, ~35 lines)

Removes Play, Shop, and MoreGames buttons from HUD and destroys them:

```cpp
void MainScreen::DeleteMenuButtons() {
    for each button in {pPlayButton, pDojoButton, pMoreGamesBtn}:
        if button != NULL:
            HUD::RemoveControl(game->hud, button)
            if button != NULL:
                button->~MenuButton()   // vtable dtor
                button = NULL
}
```

Note: does NOT remove sound/music toggles or leaderboard button.

---

## Release (0x0014cd20, ~40 lines)

Full cleanup of all resources:

```
1. SetNull global texture (blurry_backing)
2. Clear shop button's removal callback (delegate at +0x38)
3. SetNull all 12 texture SmartPtrs:
   +0x74, +0xd4 (model), +0xcc, +0xc4, +0xd0, +0xc8,
   +0x88, +0x8c, +0x114, +0x98, +0x90, +0x94
4. Zero all 6 button pointers (+0x9c..+0xb0)
5. Delete + free font (if not null): Font::~Font + operator_delete
6. Zero font pointer (+0x11c)
```

---

## Callbacks — FULLY DECOMPILED

### GameModeCallback (0x0014b068)

```cpp
void MainScreen::GameModeCallback() {
    m_State = 0x0e;
    m_Timer2 = 1.0f;
    TutorialControl::ResetTutePos(game->tutorial, NULL);
    FruitSaveData::DownloadTweaks();
    pLeaderboardBtn = NULL;
    InitVec3(game->field_0x194);    // reset camera target
}
```

### NewGameCallback (0x0014c384)

```cpp
void MainScreen::NewGameCallback() {
    CancelNews();
    m_State = 2;
    GameSound::SFXPlay(game->gameSound, "swoosh_sound", 1.0, 1.0, NULL);
    InitVec3(game->field_0x194);
}
```

### AboutCallback (0x0014afc4)

```cpp
void MainScreen::AboutCallback() {
    CancelNews();
    m_State = 4;
    m_Timer2 = 1.0f;
    TutorialControl::ResetTutePos(game->tutorial, NULL);
    pLeaderboardBtn = NULL;
}
```

### SoundCallback (0x0014af64)

```cpp
void MainScreen::SoundCallback() {
    game->soundEnabled ^= 1;                    // toggle flag at +0x44
    SoundManager::GetInstance();
    float vol = game->soundEnabled ? 0.5f : 0.0f;
    SoundManager::SetSFXVolume(vol);
}
```

### MusicCallback (0x0014ac9c)

```cpp
void MainScreen::MusicCallback() {
    game->musicEnabled ^= 1;                    // toggle flag at +0x45
    // Note: no direct music play/stop call — just flips flag.
    // Music system checks this flag elsewhere.
}
```

### LeaderboardsCallback (0x0014b010)

```cpp
void MainScreen::LeaderboardsCallback() {
    CancelNews();
    m_State = 9;    // → NetworkManager::LaunchDashboard (skip for port)
}
```

### MoreGamesCallback (0x0014b000)

```cpp
void MainScreen::MoreGamesCallback() {
    CancelNews();
    m_State = 10;   // → NetworkManager (skip for port)
}
```

### QuitGamesCallback (0x0014b1a0)

```cpp
void MainScreen::QuitGamesCallback() {
    SystemManager::RequestQuit();
    // Animate leaderboard button: set flag at piece+0x80, scale piece ×10.0
    int piece = pLeaderboardBtn->field_0x134;
    piece->field_0x80 = 1;
    piece->scale = pLeaderboardBtn->scale * 10.0;
    m_State = 0x17;     // → wait for entities → bomb flash → quit
}
```

---

## Button Layout (verified from read_memory)

All positions verified from binary constants. Coordinate system: HUDControl3d::Draw applies `offset = HUD_global × Vec3(480, 320, 0) + control.pos` before matrix translate. HUD_global is normally (1.0, 1.0, 1.0).

| Button | Texture | Position (x, y, z) | Scale | FruitType | Callback |
|--------|---------|-------------------|-------|-----------|----------|
| Sound toggle | sound.tex / sound_cross.tex | **(216.0, 135.5, 0.0)** | 32×32 | -1 (none) | SoundCallback |
| Music toggle | music.tex / music_cross.tex | **(176.0, 135.5, 0.0)** | 32×32 | -1 (none) | MusicCallback |
| New Game | newgame.tex | **(16.0, -66.0, 0.0)** | auto (tex size) | 3 (watermelon) | GameModeCallback |
| Dojo | dojo_icon.tex | **(-144.0, -65.0, 0.0)** | 0.9× / 1.05× | "mango" (FruitType lookup) | AboutCallback (→DojoScreen) |
| Leaderboard | openfeint.tex | **(182.0, -106.0, 0.0)** | 1.0× | (GOT ref) | LeaderboardsCallback |
| MoreGames | gc_achievements.tex | **(182.0, -106.0, 0.0)** | 1.0× | "kiwifruit" | MoreGamesCallback |

**MainScreen itself:** size = (480.0, 138.0, 1.0), pos = (0.0, 91.0, 0.0) where 91 = (320 - 138) / 2

All buttons are 0x15C bytes (`MenuButton`), created via `operator_new(0x15c)` + `MenuButton::MenuButton(texture, position, callback, fruitType, scale, deletedCallback)`.

### Button Creation Pattern

```cpp
// Example: Play button in state 0→1
SmartPtr<Texture> tex(m_TexNewGame);
Vec3 pos(16.0f, -66.0f, -50.0f);
Delegate0<void> callback = MakeDelegate(this, &MainScreen::GameModeCallback);
Delegate0<void> deletedCallback = MakeDelegate(NULL);
pPlayButton = new MenuButton(tex, pos, callback, 3/*fruitType*/, Vec3::Zero(), deletedCallback);
pPlayButton->LoadContent();
HUD::AddControl(game->hud, pPlayButton, false);
// Set layer = 8, set singular
pPlayButton->field_0x34 = 8;
HUDControl::SetSingular(pPlayButton);
// Set tutorial pos
TutorialControl::ResetTutePos(game->tutorial, pPlayButton);
```

---

## Timing Constants

| Constant | Value | Usage |
|----------|-------|-------|
| Camera lerp rate | 0.125 | `camera += (-1.0 - camera) * 0.125` per frame |
| Camera threshold | -0.999 | Transition: camera must be < -0.999 |
| Timer2 threshold | 0.15 | State 0: must exceed 0.15 to advance |
| State 0xe decay | 0.85 | `timer2 *= 0.85` each frame |
| State 0xe threshold | 0.25 | Create GameModeScreen when timer2 crosses 0.25 |
| State 2 decay | 0.75 | `camera *= 0.75` each frame |
| State 8 lerp rate | 0.125 | `timer2 += (1.0 - timer2) * 0.125` |
| State 8 duration | 1.5s | After timer2 reaches 1.0, wait 1.5s |
| State 8 reset timer2 | -0.85 | Reset value after slide-in |
| Logo bounce loss | -0.25 | `velocity *= -0.25` on floor hit |
| Logo settle threshold | 3.0 | Stop bouncing when abs(velocity) < 3.0 |
| Alpha lerp rate | 0.25 | `alpha += (target - alpha) * 0.25` |
| Pause visibility | 0.01 | Buttons active when pauseAmount > 0.01 |
| Sound volume on | 0.5 | SFX volume when enabled |
| HitMenuBomb pos | (163, -96, 0) | State 0x17 bomb effect position |
| Screen reference | 480×320 landscape | Game coordinate space (X=wide, Y=narrow) |

---

## Vtable (verified from binary at 0x1E9A50, 15 entries)

MainScreen : HUDControl3d. The vtable pointer is set to base+8 in the constructor.

| VTable Offset | Address | Name | Notes |
|--------------|---------|------|-------|
| +0x00 | 0x14CF60 | ~MainScreen (deleting) | |
| +0x04 | 0x14CE10 | ~MainScreen | |
| +0x08 | 0x14AC80 | Init() | calls Reset via vtable+0x10 |
| +0x0c | 0x14CD20 | Release() | ~40 lines, cleanup all textures+font |
| +0x10 | 0x14AC8C | Reset() | no-op |
| +0x14 | 0x12F92C | BeginDraw(float) | no-op (inherited from HUDControl3d) |
| +0x18 | 0x14AC90 | PreDraw(float*) | returns param (no-op) |
| +0x1c | 0x14D4EC | **Draw(float*)** | 171 lines — render background, logo, buttons |
| +0x20 | 0x12F930 | PreDrawOrder(float*,int) | dispatches to vtable+0x18 (PreDraw) |
| +0x24 | 0x12F93C | DrawOrder(float*,int) | dispatches to vtable+0x1c (Draw) |
| +0x28 | 0x14B278 | **Update(float)** | 678 lines — full state machine |
| +0x2c | 0x12FD54 | SetToMultiplayerState() | inherited |
| +0x30 | 0x12F948 | GetType() | returns 1 |
| +0x34 | 0x12F94C | Skip() | no-op |
| +0x38 | 0x12F950 | Save() | no-op |

**Draw dispatch**: HUD::Draw calls PreDrawOrder (+0x20) then DrawOrder (+0x24). These are wrappers that call the actual PreDraw (+0x18) and Draw (+0x1c) respectively. This indirection allows the HUD layer system to work.

## Key Functions

| Function | Address | Lines | Status | Purpose |
|----------|---------|-------|--------|---------|
| MainScreen ctor | 0x0014c430 | 159 | ✅ | Load 16 textures + font, init state |
| Update | 0x0014b278 | 678 | ✅ | Full state machine (14 states) |
| Draw | 0x0014d4ec | 171 | ✅ | Render background, logo, loading symbol |
| Hide | 0x0014ad04 | 7 | ✅ | Set state=0x11, zero position |
| UpdateScreenElements | 0x0014ad3c | 60 | ✅ | Bouncing logo physics + alpha lerp |
| DeleteMenuButtons | 0x0014aee8 | 35 | ✅ | Remove Play/Shop/MoreGames from HUD |
| Release | 0x0014cd20 | 40 | ✅ | SetNull all textures, free font |
| Init | 0x0014ac80 | 13 | ✅ | Calls Reset |
| PreDraw | 0x0014ac90 | 1 | ✅ | Returns param (no-op) |
| Reset | 0x0014ac8c | 1 | ✅ | No-op |
| GameModeCallback | 0x0014b068 | 10 | ✅ | → state 0xe (mode selection) |
| NewGameCallback | 0x0014c384 | 15 | ✅ | → state 2 (direct game start) + SFX |
| AboutCallback | 0x0014afc4 | 8 | ✅ | → state 4 (→ DojoScreen) |
| SoundCallback | 0x0014af64 | 8 | ✅ | Toggle soundEnabled, set volume 0.5/0.0 |
| MusicCallback | 0x0014ac9c | 4 | ✅ | Toggle musicEnabled flag only |
| LeaderboardsCallback | 0x0014b010 | 3 | ✅ | → state 9 (skip for port) |
| MoreGamesCallback | 0x0014b000 | 3 | ✅ | → state 10 (skip for port) |
| QuitGamesCallback | 0x0014b1a0 | 12 | ✅ | RequestQuit → state 0x17 (bomb) |

---

## For Porting

### States to implement
- **Core flow:** 0 (splash→menu), 1 (active menu), 0xe/0xf (→ GameModeScreen), 8 (slide-in return)
- **Game exit:** 2 (direct start), 0x11 (camera fade), 3/4 (→ DojoScreen)
- **Quit:** 0x17 → 0x18 (bomb flash → quit)
- **Skip:** 9/10 (GameCenter), 0xb (news), 0x10 (matchmaker)

### Assets needed
All textures in `Data/textures/`, font in `Data/fonts/`:
```
newgame.tex, dojo_icon.tex, quit.tex, openfeint.tex, more_games.tex,
gc_achievements.tex, comming_soon.tex, slice_fruit.tex,
sound.tex, sound_cross.tex, music.tex, music_cross.tex,
blurry_backing.tex, fruit_text.tex, ninja_text.tex,
fonts/verdana.fnt + verdana_0.tex
```

### Dependencies
- HUDControl3d — ✅ fully analyzed
- MenuButton (0x15C) — ✅ fully analyzed
- Font::Load + DrawString — ✅ fully analyzed
- FruitCamera — ✅ fully analyzed
- DisplayManager — ✅ fully analyzed
- GameSound::SFXPlay — ✅ analyzed
- TextureManager::LoadLocalisedTexture — ✅ pattern known
- HUD::AddControl/RemoveControl — ✅ fully analyzed (list push_back/remove)
- SmartPtr\<T\> — replace with std::shared_ptr
- MusicCallback — ✅ trivial (just flips flag, no music API call)

---

## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- HUD, HUDControl, HUDControl3d, all HUD functions
- [Common screen patterns](common-patterns.md) -- BaseScreen, button creation
- [GameModeScreen](game-mode.md) -- target of state 0xe
- [ShopScreen](shop.md) -- target of shop button
- [AboutScreen](about.md) -- target of about button
- [DojoScreen](dojo.md) -- target of states 3/4
