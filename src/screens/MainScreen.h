#ifndef FN_MAIN_SCREEN_H
#define FN_MAIN_SCREEN_H

//
// MainScreen : HUDControl3d
// v1.6.1 ctor 0x0019811c, Update 0x00196e1c, Draw 0x001993ac,
//   UpdateScreenElements 0x00195a58
// sizeof(MainScreen) == 0x12C (300 bytes); base HUDControl3d ends at 0x7C.
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
    // Port-specific: binary ctor takes no args (_ZN10MainScreenC1Ev @ 0x0019811c);
    //   port takes Game& because Game is reachable via *GOT[0x7990] in binary.
    MainScreen(Game& g);
    ~MainScreen();

    // HUDControl overrides (vtable order from R4 W2 RE section 2)
    void Init() override;                               // vtable slot 2 @ 0x0014AC80
    void Release() override;                            // vtable slot 3 @ 0x0014CD20
    void Reset() override;                              // vtable slot 4 @ 0x0014AC8C (no-op)
    // Binary @ 0x0014AC94 — vtable slot 6, no-op stub; binary @ 0x0014AC94
    // Defunct: vtable PreDraw — no-op stub; v1.6.1 MainScreen @ 0x0014AC94
    void PreDraw(float* hudScale) override;
    void Update(float dt) override;                     // vtable slot 10 @ 0x00196e1c
    // Binary signature: Draw(float*) at vtable slot 7 @0x001993ac (v1.6.1)
    void Draw(float* hudScaleRaw) override;
    int GetType() override { return 1; }                // vtable slot 12 (base default)

    // UpdateScreenElements — vtable slot 15; v1.6.1 MainScreen::UpdateScreenElements @ 0x00195a58
    // Binary signature: (float dt, float stateVar) — dt drives physics, stateVar gates settle.
    void UpdateScreenElements(float dt, float transitionTimer);

    // Direct state writer used by child screens.
    void SetState(MainScreenState s);

    // SetStateTimer: sets m_StateTimer (+0x110) = bounce velocity accumulator.
    // Binary @ 0x00169e80 (QuitToMenu @0x00169e50) writes 0.5f here to seed the logo
    // bounce animation on gameplay->menu return. NOT a transition countdown.
    void SetStateTimer(float t) { m_StateTimer = t; }

    // SetMoreGamesTimer: seeds m_MoreGamesF0 (the intro-slide hold countdown).
    // Binary QuitToMenu @0x001cb6e4 writes 0.5f to +0x11C to hold the slide off-screen
    // for ~0.5s before the intro plays on gameplay->menu return.
#ifndef __bada__
    void SetMoreGamesTimer(float t) { m_MoreGamesF0 = t; }
#endif // !defined(__bada__)

    // Used by EndRetryLevel to emulate GameInit step 11 (fresh MainScreen ctor).
    void ResetTimers() { m_StateTimer = 0.0f; m_Timer2 = 0.0f; }

    // @ 0x0016bbb0 — post-effect overlays drawn after HUD layer 0x08.
    void DrawPostEffects();

    // Camera-transition accessors for child screens.
    float GetCameraTransition() const;
    void  SetCameraTransition(float v);

    // Drop the four menu buttons (Play/Dojo/MoreGames/Quit).
    void DeleteMenuButtons();

public:
    // -----------------------------------------------------------------------
    // v1.6.1 binary layout; base HUDControl3d ends at 0x7C.
    // ctor 0x0019811c.
    // Fields are public so __builtin_offsetof static_asserts can access them.
    // -----------------------------------------------------------------------

    // +0x7c  bool m_bFlag7c (ctor =0; screen flag)
    bool m_bFlag7c;                                    // +0x7c
    uint8_t  _pad_7d[3];                               // alignment pad to +0x80

    // +0x80  float[3] m_Field80 (12 bytes; ctor writes here; role unclear)
    float m_Field80[3];                                // +0x80 .. +0x8b

    // +0x8c  SmartPtr<Texture> m_TexSoundOn (sound.tex)
    // +0x90  SmartPtr<Texture> m_TexSoundOff (sound_cross.tex)
    Mortar::SmartPtr<Mortar::Texture> m_TexSoundOn;   // +0x8c
    Mortar::SmartPtr<Mortar::Texture> m_TexSoundOff;  // +0x90

    // +0x94  SmartPtr<Texture> m_Tex3 (sound.tex duplicate slot)
    // +0x98  SmartPtr<Texture> m_Tex4 (sound_cross.tex duplicate slot)
    // +0x9c  SmartPtr<Texture> (third SmartPtr slot; role unclear from ctor)
    Mortar::SmartPtr<Mortar::Texture> m_Tex3;         // +0x94
    Mortar::SmartPtr<Mortar::Texture> m_Tex4;         // +0x98
    Mortar::SmartPtr<Mortar::Texture> m_Tex9c;        // +0x9c

    // +0xa0..+0xb8: 7x MenuButton* (pPlayButton..pMusicToggle)
    MenuButton* pPlayButton;                           // +0xa0
    MenuButton* pDojoButton;                           // +0xa4
    MenuButton* pLeaderboardBtn;                       // +0xa8 (port: used as Quit button)
    MenuButton* pMoreGamesBtn;                         // +0xac
    MenuButton* pToggleA;                              // +0xb0  (NEW in v1.6.1)
    MenuButton* pToggleB;                              // +0xb4  (NEW in v1.6.1)
    MenuButton* pMusicToggle;                          // +0xb8

    // +0xbc  SmartPtr<Texture> m_TexBc (comming_soon / bottom credit)
    Mortar::SmartPtr<Mortar::Texture> m_TexBc;        // +0xbc

    // +0xc0  MenuButton* pSoundToggle
    MenuButton* pSoundToggle;                          // +0xc0

    // +0xc4, +0xc8: two 4-byte slots — no ctor store; role unresolved.
    // TODO: v1.6.1 0x0019811c — confirm +0xc4/+0xc8 role (no ctor store observed)
    uint32_t _pad_c4;                                  // +0xc4
    uint32_t _pad_c8;                                  // +0xc8

    // +0xcc..+0xd8: 4x SmartPtr<Texture>
    Mortar::SmartPtr<Mortar::Texture> m_TexMusicOn;   // +0xcc
    Mortar::SmartPtr<Mortar::Texture> m_TexMusicOff;  // +0xd0
    Mortar::SmartPtr<Mortar::Texture> m_TexSliceFruit;// +0xd4
    Mortar::SmartPtr<Mortar::Texture> m_TexD;         // +0xd8

    // +0xdc  SmartPtr<Model> m_Model (only Model SmartPtr ctor in binary @ 0x001158c4)
    Mortar::SmartPtr<Mortar::Model> m_Model;          // +0xdc

    // +0xe0  BakedStringBox* m_pSliceInstrBox (new(0xc8))
    Mortar::BakedStringBox* m_pSliceInstrBox;         // +0xe0

    // +0xe4  SmartPtr<Texture> m_TexFruitText (fruit_text.tex)
    Mortar::SmartPtr<Mortar::Texture> m_TexFruitText; // +0xe4

    // +0xe8  Vec3 m_LogoPos (fruit_text + sliceInstrBox draw pos; 12 bytes)
    Vec3 m_LogoPos;                                   // +0xe8

    // +0xf4  float m_Lean (logo lean lerp, init 1.0)
    float m_Lean;                                     // +0xf4

    // +0xf8..+0x100  fruit_text sprite draw position triple
    float m_NinjaTextX;   // +0xf8
    float m_NinjaTextY;   // +0xfc
    float m_NinjaTextZ;   // +0x100

    // +0x104..+0x10c  ninja_text sprite draw position triple
    float m_BounceVel;    // +0x104  (ninja_text X; 60.0 decorative per frame)
    float m_BounceY;      // +0x108  (bounce POSITION, ninja sprite Y)
    float m_field10C;     // +0x10c  (= 0; ninja sprite Z)

    // +0x110  float m_StateTimer — BOUNCE VELOCITY accumulator.
    // UpdateScreenElements: m_StateTimer += dt*-55; m_BounceY += m_StateTimer*dt*15.
    // floor reflect: m_StateTimer *= -0.25; settle to 0 when |vel|<3 && time>0.99 && dt>0.
    // QuitToMenu @0x00169e80 writes 0.5f here to seed the logo bounce.
    float m_StateTimer;   // +0x110

    // +0x114  float m_Field114 — loading-spinner accumulator; states 0x13/0x14: +=dt*8; wrap 8.0
    float m_Field114;     // +0x114

    // +0x118  int m_State — state machine
    int m_State;          // +0x118

    // +0x11c  SmartPtr<Texture> m_TexMoreGames
    // In binary (ARM32) this slot is overloaded as the intro-slide f0 countdown
    // (MoreGames texture defunct -- always null). Port keeps this as SmartPtr and
    // stores f0 in the dedicated m_MoreGamesF0 below.
    // ASM-spec v1.6.1 MainScreen::Update @0x00197430: f0-countdown gates the intro slide.
    Mortar::SmartPtr<Mortar::Texture> m_TexMoreGames; // +0x11c

    // +0x120  SmartPtr<Texture> m_Tex120 (music.tex)
    Mortar::SmartPtr<Mortar::Texture> m_Tex120;       // +0x120

    // +0x124  float m_Timer2 — state-machine transition timer.
    float m_Timer2;       // +0x124

    // +0x128  Font* m_pFont (verdana.fnt) — stored as SmartPtr on Bada (4 bytes); port uses SmartPtr<Font>.
    Mortar::SmartPtr<Mortar::Font> m_pFont;           // +0x128

    // sizeof on Bada (ARM32, SmartPtr=4 bytes) = 0x12c

private:
    // -----------------------------------------------------------------------
    // Port-specific trailing fields (not in the binary struct).
    // Excluded on __bada__ so sizeof stays at 0x12c.
    // -----------------------------------------------------------------------
#ifndef __bada__
    // Port specific: binary accesses Game via GOT; port stores a reference here.
    Game& game;

    // DIFFERS: binary overloads m_TexMoreGames's 4-byte slot (+0x11c) as the intro f0 countdown
    // (ARM32 4-byte ptr, MoreGames texture defunct -- v1.6.1 MainScreen::Update @0x00197430).
    // The x64 port's SmartPtr is 8 bytes and m_TexMoreGames is a live pointer, so aliasing a
    // float onto it corrupts the pointer and crashes ~SmartPtr at shutdown -- use a dedicated
    // float field instead. Functional behaviour of the f0 hold/settle/exit logic is identical.
    float m_MoreGamesF0;

    // Font SmartPtr for the BakedStringBox (binary manages this differently; port convenience).
    Mortar::SmartPtr<Mortar::Font> m_BakedStrSmart;

    // Camera transition lives on game_work.m_GameDt (binary single source of truth).
    float m_GlobalAlphaTarget;
    float m_Time;

    // One-shot latch so STATE_GAME_START fires WaveManager::Reset once per entry.
    bool m_bGameStartReset;

    // Weak pointer to current DojoScreen child; cleared by RemoveCallback.
    DojoScreen* m_pDojoScreen;
#endif // !defined(__bada__)

#ifndef __bada__
    float& TexMoreGamesF0() { return m_MoreGamesF0; }
    float  TexMoreGamesF0() const { return m_MoreGamesF0; }
#endif // !defined(__bada__)


    // --- Internal helpers ---
    void Hide();
    void CreateToggles();
    void CreatePlayDojo();
    void CreateQuitButton();
    void RemoveButton(MenuButton*& btn);

    // v1.6.1 MainScreen::DrawLoadingSymbol @ 0x001154d4
    // 8-segment radial loading spinner.
    void DrawLoadingSymbol(const float* hudScale);

    // Matches MainScreen::ButtonDeleted @ 0x0014acc0.
    void ButtonDeleted(HUDControl* ctrl);

#ifndef __bada__
    void DojoScreenRemoved(HUDControl*)    { m_pDojoScreen = nullptr; }
#endif // !defined(__bada__)

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
#ifndef __bada__
    float m_TimeRemainingDisplay;
#endif // !defined(__bada__)
private:

};

#ifdef __bada__
#include <cstddef>
// Binary offsets (ARM32, SmartPtr=4 bytes, sizeof(MainScreen)==0x12c)
static_assert(__builtin_offsetof(MainScreen, m_bFlag7c)        == 0x7c,  "m_bFlag7c offset");
static_assert(__builtin_offsetof(MainScreen, m_Field80)        == 0x80,  "m_Field80 offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOn)     == 0x8c,  "m_TexSoundOn offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOff)    == 0x90,  "m_TexSoundOff offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex3)           == 0x94,  "m_Tex3 offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex4)           == 0x98,  "m_Tex4 offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex9c)          == 0x9c,  "m_Tex9c offset");
static_assert(__builtin_offsetof(MainScreen, pPlayButton)      == 0xa0,  "pPlayButton offset");
static_assert(__builtin_offsetof(MainScreen, pDojoButton)      == 0xa4,  "pDojoButton offset");
static_assert(__builtin_offsetof(MainScreen, pLeaderboardBtn)  == 0xa8,  "pLeaderboardBtn offset");
static_assert(__builtin_offsetof(MainScreen, pMoreGamesBtn)    == 0xac,  "pMoreGamesBtn offset");
static_assert(__builtin_offsetof(MainScreen, pToggleA)         == 0xb0,  "pToggleA offset");
static_assert(__builtin_offsetof(MainScreen, pToggleB)         == 0xb4,  "pToggleB offset");
static_assert(__builtin_offsetof(MainScreen, pMusicToggle)     == 0xb8,  "pMusicToggle offset");
static_assert(__builtin_offsetof(MainScreen, m_TexBc)          == 0xbc,  "m_TexBc offset");
static_assert(__builtin_offsetof(MainScreen, pSoundToggle)     == 0xc0,  "pSoundToggle offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMusicOn)     == 0xcc,  "m_TexMusicOn offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMusicOff)    == 0xd0,  "m_TexMusicOff offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSliceFruit)  == 0xd4,  "m_TexSliceFruit offset");
static_assert(__builtin_offsetof(MainScreen, m_TexD)           == 0xd8,  "m_TexD offset");
static_assert(__builtin_offsetof(MainScreen, m_Model)          == 0xdc,  "m_Model offset");
static_assert(__builtin_offsetof(MainScreen, m_pSliceInstrBox) == 0xe0,  "m_pSliceInstrBox offset");
static_assert(__builtin_offsetof(MainScreen, m_TexFruitText)   == 0xe4,  "m_TexFruitText offset");
static_assert(__builtin_offsetof(MainScreen, m_LogoPos)        == 0xe8,  "m_LogoPos offset");
static_assert(__builtin_offsetof(MainScreen, m_Lean)           == 0xf4,  "m_Lean offset");
static_assert(__builtin_offsetof(MainScreen, m_NinjaTextX)     == 0xf8,  "m_NinjaTextX offset");
static_assert(__builtin_offsetof(MainScreen, m_BounceVel)      == 0x104, "m_BounceVel offset");
static_assert(__builtin_offsetof(MainScreen, m_BounceY)        == 0x108, "m_BounceY offset");
static_assert(__builtin_offsetof(MainScreen, m_StateTimer)     == 0x110, "m_StateTimer offset");
static_assert(__builtin_offsetof(MainScreen, m_Field114)       == 0x114, "m_Field114 offset");
static_assert(__builtin_offsetof(MainScreen, m_State)          == 0x118, "m_State offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMoreGames)   == 0x11c, "m_TexMoreGames offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex120)         == 0x120, "m_Tex120 offset");
static_assert(__builtin_offsetof(MainScreen, m_Timer2)         == 0x124, "m_Timer2 offset");
static_assert(__builtin_offsetof(MainScreen, m_pFont)          == 0x128, "m_pFont offset");
static_assert(sizeof(MainScreen)                               == 0x12c, "MainScreen size");
#endif

#endif
