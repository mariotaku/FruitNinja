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
#include "math/_Vector3.h"
#include "render/Font.h"
#include "render/BakedStringBox.h"
#include "game/GameWork.h"

struct Game;
class MenuButton;
class DojoScreen;
class GameModeScreen;

// Declared in hud/MenuButton.h (v1.6.1 @0x0016ac7c); forward-declared here so
// TriggerQuitFromSettings() below can call it without pulling in the full
// MenuButton class definition.
void ClearMenuItems();

// State enum (verified from v1.6.1 binary; Update @0x00196e1c)
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
    STATE_QUIT_WAIT        = 0x15, // Tutorial reset -> bomb transition; v1.6.1 @0x00197700
    STATE_QUIT_BOMB        = 0x16, // BombFlash -> Mortar::SystemManager::QuitGame; v1.6.1 @0x00196e1c
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
    // Defunct: vtable PreDraw — no-op stub; (v1.6.1: symbol absent -- defunct/inlined)
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
    // NOT written by QuitToMenu @0x001cb6e4 (v1.6.1). The real quit write is +0x11c
    // via SetMoreGamesTimer. Used internally by UpdateScreenElements physics.
    void SetStateTimer(float t) { m_StateTimer = t; }

    // SetMoreGamesTimer: seeds the intro-slide hold countdown at +0x11c.
    // Binary QuitToMenu @0x001cb6e4 writes 0.5f to +0x11c (vstr s15,[r1,#0x11c])
    // to hold the slide off-screen for ~0.5s before the intro plays on gameplay->menu return.
    // Under __bada__: +0x11c is the raw-pointer slot of m_TexMoreGames (4 bytes, ARM32);
    // the binary writes a float there directly (MoreGames texture is defunct/null).
    // Under host: m_MoreGamesF0 is a dedicated float (SmartPtr at +0x11c is 8 bytes on x64).
    void SetMoreGamesTimer(float t) {
#ifdef __bada__
        // Binary faithful: vstr s15,[r1,#0x11c] — write float into the raw-pointer slot.
        *reinterpret_cast<float*>(&m_TexMoreGames) = t;
#else
        m_MoreGamesF0 = t;
#endif
    }

    // Used by EndRetryLevel to emulate GameInit step 11 (fresh MainScreen ctor).
    void ResetTimers() { m_StateTimer = 0.0f; m_Timer2 = 0.0f; }

    // @ 0x0016bbb0 — post-effect overlays drawn after HUD layer 0x08.
    void DrawPostEffects();

    // Camera-transition accessors for child screens.
    float GetCameraTransition() const;
    void  SetCameraTransition(float v);

    // Port specific: true only while MainScreen is parked in the gameplay-resident
    // camera-fade state (0x11) -- a mode is being PLAYED (not menu/shop/dojo/game-over).
    // Entered by GameModeScreen play cases 3-6, retry (BombHit @0x0016a276), Hide();
    // left only by QuitToMenu / GameOver -> STATE_CAMERA_ZOOM. v1.6.1 MainScreen::Update @0x00197828.
    bool IsInGameplay() const { return m_State == STATE_CAMERA_FADE; }

    // Drop the four menu buttons (Play/Dojo/MoreGames/Quit).
    void DeleteMenuButtons();

public:
    // -----------------------------------------------------------------------
    // v1.6.1 binary layout; base HUDControl3d ends at 0x7C.
    // ctor 0x0019811c.
    // Fields are public so __builtin_offsetof static_asserts can access them.
    // -----------------------------------------------------------------------

    // +0x7c  bool m_ButtonsCreatedFlag (ctor=0; set=1 by CreateButtons; gates case-1 re-create)
    // v1.6.1 MainScreen ctor @0x0019811c; CreateButtons @0x001961f8
    bool m_ButtonsCreatedFlag;                         // +0x7c
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

    // +0xa0..+0xb8: 7x MenuButton* (m_pGameModeButton..pMusicToggle)
    MenuButton* m_pGameModeButton;                     // +0xa0 (GameModeCallback)
    MenuButton* m_pStoreButton;                        // +0xa4 (AboutCallback->DojoScreen, AreNewItems badge)
    MenuButton* m_pQuitButton;                         // +0xa8 (QuitGamesCallback)
    MenuButton* m_pMoreGamesBtn;                       // +0xac
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
    _Vector3<float> m_LogoPos;                                   // +0xe8

    // +0xf4  float m_Lean (logo lean lerp, init 1.0)
    float m_Lean;                                     // +0xf4

    // +0xf8..+0x100  fruit_text sprite draw position triple
    float m_NinjaTextX;   // +0xf8
    float m_NinjaTextY;   // +0xfc
    float m_NinjaTextZ;   // +0x100

    // +0x104..+0x10c  ninja_text sprite draw position triple
    float m_BounceVel;    // +0x104  (ninja_text X; 60.0 decorative per frame)
    float m_BounceY;      // +0x108  (bounce POSITION, ninja sprite Y)
    float m_BounceZ;      // +0x10c  (= 0; ninja sprite Z)

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
    // DIFFERS: binary overloads m_TexMoreGames's 4-byte slot (+0x11c) as the intro f0 countdown
    // (ARM32 4-byte ptr, MoreGames texture defunct -- v1.6.1 MainScreen::Update @0x00197430).
    // The x64 port's SmartPtr is 8 bytes and m_TexMoreGames is a live pointer, so aliasing a
    // float onto it corrupts the pointer and crashes ~SmartPtr at shutdown -- use a dedicated
    // float field instead. Functional behaviour of the f0 hold/settle/exit logic is identical.
    float m_MoreGamesF0;

    // Lazy fallback font for the plate BakedStringBox, used only if game_work.m_pTTFFontMain
    // is still null when this ctor runs (PreloadFontsTTF hasn't populated it yet).
    Mortar::SmartPtr<Mortar::Font> m_BakedStrSmart;

    // Camera transition lives on game_work.m_PauseAmount (binary single source of truth).
    // (The logo-lean target formerly kept here is now UpdateScreenElements' function-local
    // static `s_Tute`, matching the binary's static local -- see MainScreen.cpp.)
    float m_Time;

    // One-shot latch so STATE_GAME_START fires WaveManager::Reset once per entry.
    bool m_bGameStartReset;

    // Port specific: no binary counterpart. Bottom-left SETTINGS button that
    // opens the SettingsScreen modal (mirrors the sound/music toggle
    // lifecycle -- created inline in Update(), never torn down while
    // MainScreen persists). See MainScreen::Update and SettingsCallback.
    //
    // Slide/scale/m_Active are driven DIRECTLY off `elapsedTime` each frame
    // in Update() -- the same factor that positions MainScreen's own logo/
    // content and (via the state-dependent override switch) that IS m_Timer2
    // during the ring-tapped leave states. No separate eased-follower field:
    // an earlier version re-eased elapsedTime through its own CAMERA_LERP_RATE
    // lerp (plus a fixed show-delay), which made the button visibly lag both
    // the enter and leave transitions relative to the three ring MenuButtons
    // (m_pGameModeButton/m_pStoreButton/m_pQuitButton). Re-easing an already-
    // eased/decayed value only adds lag; reading elapsedTime raw keeps this
    // button in lockstep with the rings every frame.
    MenuButton* m_pSettingsButton;
#endif // !defined(__bada__)

    // Intro-slide hold countdown, defined on BOTH builds so MainScreen::Update's
    // case-0 two-way branch and the case-8 exit compile unconditionally (see
    // SetMoreGamesTimer above for why the storage differs per build).
    float& TexMoreGamesF0() {
#ifdef __bada__
        return *reinterpret_cast<float*>(&m_TexMoreGames);
#else
        return m_MoreGamesF0;
#endif
    }
    float TexMoreGamesF0() const {
#ifdef __bada__
        return *reinterpret_cast<const float*>(&m_TexMoreGames);
#else
        return m_MoreGamesF0;
#endif
    }


    // --- Internal helpers ---
    // NOTE: no CreateToggles helper — the binary builds both sound/music toggles
    // INLINE at the top of Update @0x00196e1c, each under its own null guard.
    // v1.6.1 MainScreen::CreateButtons @0x001961f8: gated by flM_BombHitTimer<1.45, then per-button
    // null checks; sets m_ButtonsCreatedFlag=1 on first run. Called per-frame from case 0.
    void CreateButtons();
    void CreateQuitButton();
    void RemoveButton(MenuButton*& btn);

    // v1.6.1 MainScreen::DrawLoadingSymbol @ 0x001154d4
    // 8-segment radial loading spinner. Binary sig takes non-const float* (unused for
    // writes; port only reads through it, but matches binary ABI for symbol-diff pairing).
    void DrawLoadingSymbol(float* hudScale);

    // Matches MainScreen::ButtonDeleted @ 0x0014acc0.
    void ButtonDeleted(HUDControl* ctrl);

    // --- Callbacks ---
    void GameModeCallback();
    // Binary @ 0x0014B0AC — multiplayer variant of GameModeCallback (state 0xF).
    void MultiplayerGameModeCallback();
    void NewGameCallback();
    void AboutCallback();
    void SoundCallback();
    void MusicCallback();
#ifndef __bada__
    // Port specific: no binary counterpart. Opens/closes the SettingsScreen
    // modal via SettingsScreen::Toggle().
    void SettingsCallback();
#endif // !defined(__bada__)
    void LeaderboardsCallback();
    void MoreGamesCallback();
    void QuitGamesCallback();

    // Binary @ 0x0014AFB8 — Defunct: NetworkManager::CancelNewsDisplay — no-op stub
    void CancelNews();
    // Binary @ 0x0014ACFC — Defunct: network UI button — no-op stub
    void ClearNetworkButton();
    // Defunct: leaderboard UI button — no-op stub; v1.6.1 MainScreen::CreateNormalLeaderboardButton @0x00195a08
    int CreateNormalLeaderboardButton(float x);

public:
    // Binary @ 0x0014AC98 — no-op event hook.
    void OnMenuItemsCleared();

    // Public: invoked by SkipToPause (PauseScreen.cpp) on the background-pause path,
    // matching the binary where SkipToPause @0x001cb424 calls MainScreen::Hide directly.
    void Hide();

#ifndef __bada__
    // Port specific: no binary counterpart (SettingsScreen itself has none).
    // Public forwarder so SettingsScreen::Toggle()'s quit-on-language-change
    // path can run the SAME full quit sequence as the QUIT ring button
    // (m_pQuitButton's own click callback is QuitGamesCallback, which is
    // private -- this just exposes it). Do not hand-roll the RequestQuit()+
    // m_State write here; QuitGamesCallback also arms the quit button's
    // tracked bomb's fling velocity, which the hand-rolled version skipped.
    //
    // Also fires ClearMenuItems() first, replicating the SLICE path a real
    // quit-ring tap goes through: Bomb::CollisionResponse's menu-rehit branch
    // (v1.6.1 @0x1d5d4c, src/entities/Bomb.cpp ~line 533-538) calls
    // ClearMenuItems() THEN m_HitCallback() (== QuitGamesCallback) in that
    // order, gated on m_bClearsMenuItems -- which MenuButton::Init defaults
    // to 1 for every ring button, including the quit button. Without
    // ClearMenuItems(), sibling ring buttons (GameMode/Store/Leaderboard)
    // keep their tracked Fruit entities alive forever: MenuButton::Update
    // re-parks a held entity's position every frame
    // (entity->pos = GetAdjustedPos(), v1.6.1 @0x0019a860) as long as
    // m_pEntity != nullptr, and only a slice (fruit: velocity divergence) or
    // Bomb::Disable() (bomb: !Enabled()) releases that hold. ClearMenuItems()
    // (src/hud/MenuButton.cpp @0x1cc6d0) is what performs that release in
    // bulk for every live fruit/bomb. QuitGamesCallback() alone only nudges
    // the QUIT button's own tracked bomb -- siblings stay parked, so
    // ActorManager::GetNumEntities(0) never reaches 0 and STATE_QUIT_WAIT
    // (MainScreen::Update case 0x15) hangs forever. Same gap exists on the
    // real ring-button TAP path (as opposed to a slice) -- this forwarder is
    // the one call site that needs the explicit cascade since settings never
    // slices anything.
    void TriggerQuitFromSettings() { ClearMenuItems(); QuitGamesCallback(); }
#endif // !defined(__bada__)

    // Port-specific: HUD-side timer mirror written by TimeControl::Update every frame.
    // ASM-spec v1.6.1 TimeControl::Update @0x001c0a48 (re-stamp: old addrs 0x001624f6/
    // 0x00162830 were stale BonusScreen functions; TimeControl::Update is the HUD-timer source).
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
static_assert(__builtin_offsetof(MainScreen, m_ButtonsCreatedFlag) == 0x7c,  "m_ButtonsCreatedFlag offset");
static_assert(__builtin_offsetof(MainScreen, m_Field80)        == 0x80,  "m_Field80 offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOn)     == 0x8c,  "m_TexSoundOn offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOff)    == 0x90,  "m_TexSoundOff offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex3)           == 0x94,  "m_Tex3 offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex4)           == 0x98,  "m_Tex4 offset");
static_assert(__builtin_offsetof(MainScreen, m_Tex9c)          == 0x9c,  "m_Tex9c offset");
static_assert(__builtin_offsetof(MainScreen, m_pGameModeButton) == 0xa0, "m_pGameModeButton offset");
static_assert(__builtin_offsetof(MainScreen, m_pStoreButton)    == 0xa4, "m_pStoreButton offset");
static_assert(__builtin_offsetof(MainScreen, m_pQuitButton)     == 0xa8, "m_pQuitButton offset");
static_assert(__builtin_offsetof(MainScreen, m_pMoreGamesBtn)   == 0xac, "m_pMoreGamesBtn offset");
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
