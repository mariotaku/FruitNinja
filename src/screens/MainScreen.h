#ifndef FN_MAIN_SCREEN_H
#define FN_MAIN_SCREEN_H

//
// MainScreen : HUDControl3d
// v1.6.1 re-layout: sizeof(MainScreen) == 0x12C (300 bytes) in binary (BaseScreen base).
// Port base is HUDControl3d (0x7C = 124 bytes) — 24 bytes short of BaseScreen (0x94).
// All trailing field offsets are adjusted accordingly (binary_offset - 0x18).
// Original: ctor 0x0014c430, Update 0x0014b278, Draw 0x0014d4ec
//
// TODO: Re-base MainScreen from HUDControl3d to BaseScreen (0x94 base).
//   v1.6.1 binary uses BaseScreen as the common base for DojoScreen/GameModeScreen/etc.;
//   MainScreen inherits BaseScreen per v1.6.1 layout (148B base + 152B trailing = 300B).
//   This re-base shifts all port offsets by +0x18, aligning them with binary offsets.
//   Deferred because it requires touching all MainScreen consumers that reference binary
//   offset +0x10c (m_State) and all sibling screens whose vtable dispatch assumes
//   HUDControl3d as the common base. Re-base in a dedicated session after DojoScreen +
//   GameModeScreen are both confirmed BaseScreen children.
//

#include "hud/HUDControl3d.h"
#include <cstdint>
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "math/Vec3.h"
#include "render/Font.h"
#include "render/BakedStringBox.h"
#include "game/GameWork.h"

struct Game;
class MenuButton;
class DojoScreen;
class GameModeScreen;

// State enum (verified from binary)
enum MainScreenState {
    STATE_CAMERA_ZOOM      = 0,    // Camera zoom-in, create toggles + play/dojo
    STATE_CREATE_BUTTONS   = 1,    // Create leaderboard/moregames, active menu
    STATE_GAME_START       = 2,    // Direct game start, camera fade
    STATE_DOJO_WAIT_A      = 3,    // Wait for entities -> DojoScreen
    STATE_DOJO_WAIT_B      = 4,    // Wait for entities -> DojoScreen (about)
    STATE_SLIDE_IN         = 8,    // Slide-in return transition
    STATE_LEADERBOARD      = 9,    // Network (skip)
    STATE_MORE_GAMES       = 10,   // Network (skip)
    STATE_NEWS             = 0x0b, // Network (skip)
    STATE_MODE_SELECT      = 0x0e, // Slide-out -> GameModeScreen
    STATE_MODE_SELECT_2    = 0x0f, // Slide-out continued (multiplayer variant)
    STATE_MATCHMAKER       = 0x10, // Network (skip)
    STATE_CAMERA_FADE      = 0x11, // Camera fade after game return
    STATE_LOADING_A        = 0x13, // Timer accumulate + loading symbol
    STATE_LOADING_B        = 0x14, // Timer accumulate + loading symbol
    STATE_DOJO_WAIT_C      = 0x15, // Wait for entities (variant)
    STATE_DOJO_WAIT_D      = 0x16, // Wait for entities (variant)
    STATE_QUIT_WAIT        = 0x17, // Tutorial reset -> bomb transition
    STATE_QUIT_BOMB        = 0x18, // BombFlash -> Mortar::SystemManager::QuitGame
};

class MainScreen : public HUDControl3d {
public:
    // Port-specific: binary ctor takes no args (_ZN10MainScreenC1Ev @ 0x0014C8A8);
    //   port takes Game& because Game is reachable via *GOT[0x7990] in binary.
    MainScreen(Game& g);
    ~MainScreen();

    // HUDControl overrides (vtable order from R4 W2 RE section 2)
    void Init() override;                               // vtable slot 2 @ 0x0014AC80
    void Release() override;                            // vtable slot 3 @ 0x0014CD20
    void Reset() override;                              // vtable slot 4 @ 0x0014AC8C (no-op)
    // Binary @ 0x0014AC94 — vtable slot 6, no-op stub; returns this.
    // Defunct: vtable PreDraw — no-op stub matching binary's empty body; binary @ 0x0014AC94
    void* PreDraw(float* hudScale);
    void Update(float dt) override;                     // vtable slot 10 @ 0x0014B278
    // Port-specific: binary signature is Draw(float*) at vtable slot 7 @ 0x0014D4EC;
    //   port uses Draw(Vec3&, int) for ergonomic param-passing.
    void Draw(const Vec3& hudScale, int layerMask) override;
    int GetType() override { return 1; }                // vtable slot 12 (base default)

    // UpdateScreenElements — vtable slot 15 @ 0x0014AD3C (extension slot beyond base 15)
    void UpdateScreenElements(float cameraTransition, float time);

    // Direct state writer used by child screens.
    void SetState(MainScreenState s);

    // SetStateTimer: sets m_StateTimer (+0xF8 port / +0x110 binary) = bounce velocity accumulator.
    // Binary @ 0x00169e80 (QuitToMenu @0x00169e50) writes 0.5f here to seed the logo
    // bounce animation on gameplay->menu return. NOT a transition countdown.
    void SetStateTimer(float t) { m_StateTimer = t; }

    // SetMoreGamesTimer: seeds m_MoreGamesF0 (the intro-slide hold countdown).
    // Binary QuitToMenu @0x001cb6e4 writes 0.5f to +0x11C to hold the slide off-screen
    // for ~0.5s before the intro plays on gameplay->menu return.
    void SetMoreGamesTimer(float t) { m_MoreGamesF0 = t; }

    // Used by EndRetryLevel to emulate GameInit step 11 (fresh MainScreen ctor).
    void ResetTimers() { m_StateTimer = 0.0f; m_Timer2 = 0.0f; }

    // @ 0x0016bbb0 — post-effect overlays drawn after HUD layer 0x08.
    void DrawPostEffects();

    // Camera-transition accessors for child screens.
    float GetCameraTransition() const;
    void  SetCameraTransition(float v);

    // Drop the four menu buttons (Play/Dojo/MoreGames/Quit).
    void DeleteMenuButtons();

private:
    // -----------------------------------------------------------------------
    // v1.6.1 layout (port offsets = binary offsets - 0x18 due to HUDControl3d base)
    // Binary base: BaseScreen (0x94 = 148 bytes); Port base: HUDControl3d (0x7C = 124 bytes)
    // -----------------------------------------------------------------------

    // +0x7C (binary +0x94): SmartPtr<Texture> m_Tex3 (sound.tex)
    Mortar::SmartPtr<Mortar::Texture> m_Tex3;           // +0x7c

    // +0x80 (binary +0x98): SmartPtr<Texture> m_Tex4 (sound_cross.tex)
    Mortar::SmartPtr<Mortar::Texture> m_Tex4;           // +0x80

    // +0x84..+0x9C (binary +0x9C..+0xB4): button pointers
    MenuButton* pPlayButton;                             // +0x84 (binary +0x9C)
    MenuButton* pDojoButton;                             // +0x88 (binary +0xA0)
    MenuButton* pLeaderboardBtn;  // port: used as Quit button; binary name pLeaderboardBtn @ +0xA4
    MenuButton* pMoreGamesBtn;                           // +0x90 (binary +0xA8)
    MenuButton* pToggleA;         // NEW in v1.6.1                // +0x94 (binary +0xAC)
    MenuButton* pToggleB;         // NEW in v1.6.1                // +0x98 (binary +0xB0)
    MenuButton* pMusicToggle;                            // +0x9C (binary +0xB4)
    MenuButton* pSoundToggle;                            // +0xA0 (binary +0xB8)

    // +0xA4 (binary +0xBC): SmartPtr<Texture> m_TexBc (comming_soon / bottom credit)
    Mortar::SmartPtr<Mortar::Texture> m_TexBc;          // +0xa4 (was m_TexCommingSoon)

    // +0xA8 (binary +0xC0): 4-byte unnamed gap between m_TexBc and m_TexSoundOn.
    // Binary: m_TexBc ends at +0xC0, m_TexSoundOn starts at +0xC4 (8-byte stride in binary,
    // 4 bytes of gap = one unnamed field). Port mirrors the gap.
    uint32_t _pad_0xA8;                                  // +0xa8 (binary +0xC0)

    // +0xAC..+0xB8 (binary +0xC4..+0xD0): 4 SmartPtr<Texture> for toggle textures
    Mortar::SmartPtr<Mortar::Texture> m_TexSoundOn;     // +0xAC (binary +0xC4)
    Mortar::SmartPtr<Mortar::Texture> m_TexSoundOff;    // +0xB0 (binary +0xC8)
    Mortar::SmartPtr<Mortar::Texture> m_TexMusicOn;     // +0xB4 (binary +0xCC)
    Mortar::SmartPtr<Mortar::Texture> m_TexMusicOff;    // +0xB8 (binary +0xD0)

    // +0xBC (binary +0xD4): SmartPtr<Model> m_Model2
    Mortar::SmartPtr<Mortar::Model> m_Model2;            // +0xBC (binary +0xD4)

    // +0xC0 (binary +0xD8): SmartPtr<Texture> m_TexSliceFruit (slice_fruit.tex)
    Mortar::SmartPtr<Mortar::Texture> m_TexSliceFruit;  // +0xC0 (binary +0xD8)

    // +0xC4 (binary +0xDC): SmartPtr (m_BakedStrSmart) — NEW in v1.6.1
    // Owns the font ref for the BakedStringBox (gangofchinese.ttf).
    Mortar::SmartPtr<Mortar::Font> m_BakedStrSmart;     // +0xC4 (binary +0xDC)

    // +0xC8 (binary +0xE0): BakedStringBox* m_pSliceInstrBox
    // new(200) in binary; text id 0x39d "SLICE FRUIT TO BEGIN", w75 h30 line9.0
    Mortar::BakedStringBox* m_pSliceInstrBox;            // +0xC8 (binary +0xE0)

    // +0xCC (binary +0xE4): SmartPtr<Texture> m_TexFruitText (fruit_text.tex)
    // Drawn at m_LogoPos.
    Mortar::SmartPtr<Mortar::Texture> m_TexFruitText;   // +0xCC (binary +0xE4)

    // +0xD0 (binary +0xE8): Vec3 m_LogoPos (fruit_text + sliceInstrBox draw pos)
    // Written by UpdateScreenElements. 12 bytes.
    Vec3 m_LogoPos;                                      // +0xD0 (binary +0xE8)

    // +0xDC (binary +0xF4): float m_Lean (logo lean lerp, init 1.0)
    float m_Lean;                                        // +0xDC (binary +0xF4)

    // +0xE0..+0xE8 (binary +0xF8..+0x100): fruit_text sprite draw position triple
    float m_NinjaTextX;   // +0xE0 (binary +0xF8); = -120/frame
    float m_NinjaTextY;   // +0xE4 (binary +0xFC); = pos.y+18
    float m_NinjaTextZ;   // +0xE8 (binary +0x100); = 0

    // +0xEC..+0xF4 (binary +0x104..+0x10C): ninja_text sprite draw position triple
    // m_BounceVel: = 60.0/frame decorative; also the X of the ninja_text sprite in Draw.
    // NOT the integrator velocity (m_StateTimer is the velocity).
    float m_BounceVel;    // +0xEC (binary +0x104); ninja_text sprite X
    float m_BounceY;      // +0xF0 (binary +0x108); bounce POSITION (ninja sprite Y)
    float m_field10C;     // +0xF4 (binary +0x10C); = 0; ninja sprite Z

    // +0xF8 (binary +0x110): float m_StateTimer — BOUNCE VELOCITY accumulator.
    // UpdateScreenElements: m_StateTimer += camTrans*-55; m_BounceY += m_StateTimer*camTrans*15;
    // floor reflect: m_StateTimer *= -0.25; settle to 0 when |vel|<3 && time>0.99 && camTrans>0.
    // Binary @ 0x00169e80 (QuitToMenu) writes 0.5f here to seed the logo bounce.
    float m_StateTimer;   // +0xF8 (binary +0x110)

    // +0xFC (binary +0x114): float m_Field114 — loading-spinner accumulator
    // states 0x13/0x14: += dt*8; wrap 8.0
    float m_Field114;     // +0xFC (binary +0x114)

    // +0x100 (binary +0x118): int m_State — state machine
    int m_State;          // +0x100 (binary +0x118)

    // +0x104 (binary +0x11C): SmartPtr<Texture> m_TexMoreGames
    // In the binary (ARM32) this 4-byte slot is also overloaded as the intro-slide f0
    // countdown (MoreGames texture is defunct -- always null). The port keeps this as
    // a normal live SmartPtr and stores f0 in the dedicated m_MoreGamesF0 below.
    // ASM-spec v1.6.1 MainScreen::Update @0x00197430: f0-countdown gates the intro slide;
    //   when f0 > 0.0f the slide is held off-screen and ticked down; settle branch increments
    //   m_Timer2 and ramps m_GameDt toward -1 (slide-in). QuitToMenu @0x001cb6e4 seeds
    //   f0=0.5f so gameplay->menu return holds off-screen for ~0.5s before sliding in.
    Mortar::SmartPtr<Mortar::Texture> m_TexMoreGames;   // +0x104 (binary +0x11C)

    // DIFFERS: binary overloads m_TexMoreGames's 4-byte slot (+0x11c) as the intro f0 countdown
    // (ARM32 4-byte ptr, MoreGames texture defunct -- v1.6.1 MainScreen::Update @0x00197430).
    // The x64 port's SmartPtr is 8 bytes and m_TexMoreGames is a live pointer, so aliasing a
    // float onto it corrupts the pointer and crashes ~SmartPtr at shutdown -- use a dedicated
    // float field instead. Functional behaviour of the f0 hold/settle/exit logic is identical.
    float m_MoreGamesF0;                                 // ARM32 alias of +0x11C; port-private float

    float& TexMoreGamesF0() { return m_MoreGamesF0; }
    float  TexMoreGamesF0() const { return m_MoreGamesF0; }

    // +0x108 (binary +0x120): SmartPtr<Texture> m_Tex120 (music.tex)
    Mortar::SmartPtr<Mortar::Texture> m_Tex120;         // +0x108 (binary +0x120)

    // +0x10C (binary +0x124): float m_Timer2 — state-machine transition timer.
    // Every Update state case reads/writes m_Timer2: case0 +=dt gate>0.15;
    // cases 0xe/0xf/0x10 *=0.85/0.85.
    float m_Timer2;       // +0x10C (binary +0x124)

    // +0x110 (binary +0x128): Font* m_pFont (verdana.fnt)
    Mortar::SmartPtr<Mortar::Font> m_pFont;             // +0x110 (binary +0x128)
    // Port sizeof = 0x114 = 276. Binary sizeof = 0x12C = 300 (24 more due to BaseScreen base).

    // -----------------------------------------------------------------------
    // Port-specific trailing fields (not in the binary struct).
    // Excluded on __bada__ so sizeof stays at 0x114.
    // -----------------------------------------------------------------------
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    // Port specific: binary accesses Game via GOT; port stores a reference here.
    Game& game;

    // Global textures (not on struct; loaded in ctor and assigned to file-scope globals)
    // s_blurTex/m_fruitTex/m_ninjaTex read directly in Draw.
    Mortar::SmartPtr<Mortar::Texture> m_blurryBackingTex;   // blurry_backing.tex -> s_blurTex
    Mortar::SmartPtr<Mortar::Texture> m_fruitTextTex;        // fruit_text.tex -> m_fruitTex
    Mortar::SmartPtr<Mortar::Texture> m_ninjaTextTex;        // ninja_text.tex -> m_ninjaTex

    // Camera transition lives on game_work.m_GameDt (binary single source of truth).
    float m_GlobalAlphaTarget;
    float m_Time;

    // One-shot latch so STATE_GAME_START fires WaveManager::Reset once per entry.
    bool m_bGameStartReset;

    // Weak pointer to current DojoScreen child; cleared by RemoveCallback.
    DojoScreen* m_pDojoScreen;
#endif // !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)

    // --- Internal helpers ---
    void Hide();
    void CreateToggles();
    void CreatePlayDojo();
    void CreateQuitButton();
    void RemoveButton(MenuButton*& btn);

    // Binary @ 0x0014D1F8 — 8-segment radial loading spinner.
    void DrawLoadingSymbol(const float* hudScale);

    // Matches MainScreen::ButtonDeleted @ 0x0014acc0.
    void ButtonDeleted(HUDControl* ctrl);

#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    void DojoScreenRemoved(HUDControl*)    { m_pDojoScreen = nullptr; }
#endif

    // --- Callbacks ---
    void GameModeCallback();
    // Binary @ 0x0014B0AC — multiplayer variant of GameModeCallback (state 0xF).
    void MultiplayerGameModeCallback();
    void NewGameCallback();
    void AboutCallback();
    void SoundCallback();
    void MusicCallback();
    void LeaderboardsCallback();
    void MoreGamesCallback();
    void QuitGamesCallback();

    // Binary @ 0x0014AFB8 — Defunct: NetworkManager::CancelNewsDisplay — no-op stub
    void CancelNews();
    // Binary @ 0x0014ACFC — Defunct: network UI button — no-op stub
    void ClearNetworkButton();
    // Binary @ 0x0014AD00 — Defunct: leaderboard UI button — no-op stub
    MainScreen* CreateNormalLeaderboardButton();

public:
    // Binary @ 0x0014AC98 — no-op event hook.
    void OnMenuItemsCleared();

    // Port-specific: HUD-side timer mirror written by TimeControl::Update every frame.
    // ASM-verified: 2026-05-18 binary @ 0x001624f6 / 0x00162830 (re-analyst)
    // Note: binary writes to mainScreen+0x118 (binary m_State field at that offset).
    // Port uses a dedicated member to avoid aliasing the state machine int.
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    float m_TimeRemainingDisplay;
#endif
private:

};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
// Port offsets = binary offsets - 0x18 (HUDControl3d base vs BaseScreen base)
static_assert(__builtin_offsetof(MainScreen, m_Tex3)           == 0x7c,  "m_Tex3 offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex4)           == 0x80,  "m_Tex4 offset");
static_assert(__builtin_offsetof(MainScreen, pPlayButton)      == 0x84,  "pPlayButton offset");
static_assert(__builtin_offsetof(MainScreen, pDojoButton)      == 0x88,  "pDojoButton offset");
static_assert(__builtin_offsetof(MainScreen, pLeaderboardBtn)  == 0x8c,  "pLeaderboardBtn offset");
static_assert(__builtin_offsetof(MainScreen, pMoreGamesBtn)    == 0x90,  "pMoreGamesBtn offset");
static_assert(__builtin_offsetof(MainScreen, pToggleA)         == 0x94,  "pToggleA offset");
static_assert(__builtin_offsetof(MainScreen, pToggleB)         == 0x98,  "pToggleB offset");
static_assert(__builtin_offsetof(MainScreen, pMusicToggle)     == 0x9c,  "pMusicToggle offset");
static_assert(__builtin_offsetof(MainScreen, pSoundToggle)     == 0xa0,  "pSoundToggle offset");
static_assert(__builtin_offsetof(MainScreen, m_TexBc)          == 0xa4,  "m_TexBc offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOn)     == 0xac,  "m_TexSoundOn offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMusicOff)    == 0xb8,  "m_TexMusicOff offset");
static_assert(__builtin_offsetof(MainScreen, m_Model2)         == 0xbc,  "m_Model2 offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSliceFruit)  == 0xc0,  "m_TexSliceFruit offset");
static_assert(__builtin_offsetof(MainScreen, m_BakedStrSmart)  == 0xc4,  "m_BakedStrSmart offset");
static_assert(__builtin_offsetof(MainScreen, m_pSliceInstrBox) == 0xc8,  "m_pSliceInstrBox offset");
static_assert(__builtin_offsetof(MainScreen, m_TexFruitText)   == 0xcc,  "m_TexFruitText offset");
static_assert(__builtin_offsetof(MainScreen, m_LogoPos)        == 0xd0,  "m_LogoPos offset");
static_assert(__builtin_offsetof(MainScreen, m_Lean)           == 0xdc,  "m_Lean offset");
static_assert(__builtin_offsetof(MainScreen, m_NinjaTextX)     == 0xe0,  "m_NinjaTextX offset");
static_assert(__builtin_offsetof(MainScreen, m_BounceVel)      == 0xec,  "m_BounceVel offset");
static_assert(__builtin_offsetof(MainScreen, m_BounceY)        == 0xf0,  "m_BounceY offset");
static_assert(__builtin_offsetof(MainScreen, m_StateTimer)     == 0xf8,  "m_StateTimer offset");
static_assert(__builtin_offsetof(MainScreen, m_Field114)       == 0xfc,  "m_Field114 offset");
static_assert(__builtin_offsetof(MainScreen, m_State)          == 0x100, "m_State offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMoreGames)   == 0x104, "m_TexMoreGames offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex120)         == 0x108, "m_Tex120 offset");
static_assert(__builtin_offsetof(MainScreen, m_Timer2)         == 0x10c, "m_Timer2 offset");
static_assert(__builtin_offsetof(MainScreen, m_pFont)          == 0x110, "m_pFont offset");
static_assert(sizeof(MainScreen)                               == 0x114, "MainScreen size (port: 276; binary: 300 with BaseScreen base)");
#endif

#endif
