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

// Port specific: releases MainScreen.cpp's three file-scope texture globals
// (s_blurTex / m_fruitTex / m_ninjaTex, the port's mirrors of the binary's
// MainScreen.cpp GOT globals). v1.6.1 has NO MainScreen::UnLoadContent -- the
// binary defers these to atexit via __aeabi_atexit(&MainScreen::m_fruitTex,
// ~SmartPtr) registered by global.constructors.keyed.to.MainScreen.cpp
// @0x00199a24. The port cannot follow that: its atexit runs after
// SDL_GL_DeleteContext, so the GL texture names would leak. Called from
// GameDestroy instead, before MeshManager::Destroy().
//
// The three textures are loaded unconditionally in the MainScreen ctor, so they
// are live on every boot. Idempotent; a later MainScreen ctor re-loads them.
void MainScreen_UnloadStatics();

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
    // Zero-arg, matching the binary (_ZN10MainScreenC1Ev @ 0x0019811c): the
    // prologue only ever reads r0 (this), r1 is never touched, so there is no
    // second parameter. Game is reached through Game::GetInstance() where needed.
    MainScreen();
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
    // via SetIntroHoldTimer. Used internally by UpdateScreenElements physics.
    void SetStateTimer(float t) { m_StateTimer = t; }

    // SetIntroHoldTimer: seeds the intro-slide hold countdown at +0x11c.
    // Binary QuitToMenu @0x001cb6e4 writes 0.5f to +0x11c (vstr s15,[r1,#0x11c])
    // to hold the slide off-screen for ~0.5s before the intro plays on gameplay->menu return.
    // +0x11c is a plain float in the binary -- the ctor stores 0.0f there
    // (v1.6.1 MainScreen::MainScreen @0x001987ec) and no texture is ever loaded
    // into it; more_games.tex lives at +0x120.
    void SetIntroHoldTimer(float t) { m_IntroHoldTimer = t; }

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

    // +0x8c / +0x90: ring-button art. Loaded by the ctor but never read by
    // MainScreen -- CreateButtons @0x001961f8 sources every MenuButton texture
    // from game_work.pM_Textures[]. Kept because the ctor loads them.
    Mortar::SmartPtr<Mortar::Texture> m_TexNewGame;   // +0x8c  newgame.tex
    Mortar::SmartPtr<Mortar::Texture> m_TexDojoIcon;  // +0x90  dojo_icon.tex

    // Defunct: OpenFeint / GameCenter menu art — loaded, never drawn; v1.6.1 MainScreen::MainScreen @ 0x0019811c
    // Draw @0x001993ac, Update @0x00196e1c and CreateButtons @0x001961f8 were
    // traced exhaustively and none of them reference +0x94 / +0x98. The load is
    // part of the ctor's shape (stub-don't-skip); there is no missing draw call
    // to "fix" here.
    Mortar::SmartPtr<Mortar::Texture> m_TexOpenFeint;      // +0x94  openfeint.tex
    Mortar::SmartPtr<Mortar::Texture> m_TexGCAchievements; // +0x98  gc_achievements.tex

    // +0x9c  quit.tex — same story as +0x8c/+0x90 (loaded, never read here).
    Mortar::SmartPtr<Mortar::Texture> m_TexQuit;      // +0x9c

    // +0xa0..+0xb8: 7x MenuButton*
    MenuButton* m_pGameModeButton;                     // +0xa0 (GameModeCallback)
    MenuButton* m_pStoreButton;                        // +0xa4 (AboutCallback->DojoScreen, AreNewItems badge)
    MenuButton* m_pQuitButton;                         // +0xa8 (QuitGamesCallback)
    MenuButton* m_pMoreGamesBtn;                       // +0xac
    MenuButton* pToggleA;                              // +0xb0  (NEW in v1.6.1; role unresolved)
    // +0xb4 IS the sound toggle: v1.6.1 MainScreen::Update @0x00197188-0x001971c4
    // builds it with SoundCallback at (216, 135.5) and swaps its m_Texture (+0x74)
    // between +0xd4 / +0xd8 indexed by bSoundOn^1.
    MenuButton* pSoundToggle;                          // +0xb4
    MenuButton* pMusicToggle;                          // +0xb8 (MusicCallback, (176, 135.5), pair +0xcc/+0xd0)

    // +0xbc  comming_soon overlay (binary spelling); drawn by Draw @0x001993ac
    // gated on m_pGameModeButton.
    Mortar::SmartPtr<Mortar::Texture> m_TexCommingSoon; // +0xbc

    // +0xc0, +0xc4, +0xc8: three 4-byte slots — no ctor store; role unresolved.
    // TODO: v1.6.1 0x0019811c (MainScreen::MainScreen) — confirm +0xc0/+0xc4/+0xc8 role (no ctor store observed)
    uint32_t _pad_c0;                                  // +0xc0
    uint32_t _pad_c4;                                  // +0xc4
    uint32_t _pad_c8;                                  // +0xc8

    // +0xcc..+0xd8: the two toggle texture pairs, music first then sound.
    Mortar::SmartPtr<Mortar::Texture> m_TexMusicOn;   // +0xcc  music.tex
    Mortar::SmartPtr<Mortar::Texture> m_TexMusicOff;  // +0xd0  music_cross.tex
    Mortar::SmartPtr<Mortar::Texture> m_TexSoundOn;   // +0xd4  sound.tex
    Mortar::SmartPtr<Mortar::Texture> m_TexSoundOff;  // +0xd8  sound_cross.tex

    // +0xdc  SmartPtr<Model> m_Model (only Model SmartPtr ctor in binary @ 0x0019a4ec)
    Mortar::SmartPtr<Mortar::Model> m_Model;          // +0xdc

    // +0xe0  BakedStringBox* m_pSliceInstrBox (new(0xc8))
    Mortar::BakedStringBox* m_pSliceInstrBox;         // +0xe0

    // +0xe4  slice_fruit.tex — the parchment frame Draw @0x001993ac renders at
    // m_LogoPos with the instruction string on top. The binary loads its own
    // copy here; it is NOT an alias of the fruit_text global.
    Mortar::SmartPtr<Mortar::Texture> m_TexSliceFruit; // +0xe4

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

    // +0x11c  float m_IntroHoldTimer — intro-slide hold countdown. Plain float in
    // the binary: the ctor stores 0.0f (v1.6.1 MainScreen::MainScreen @0x001987ec),
    // QuitToMenu @0x001cb6e4 stores 0.5f, and MainScreen::Update @0x00197430 reads
    // it to gate the intro slide. No texture is ever loaded into this slot.
    float m_IntroHoldTimer;                           // +0x11c

    // Defunct: more-games menu art — loaded, never drawn; v1.6.1 MainScreen::MainScreen @ 0x0019811c
    // Same as +0x94/+0x98: no Draw/Update/CreateButtons reference exists, so the
    // absent draw call is correct, not a gap.
    Mortar::SmartPtr<Mortar::Texture> m_TexMoreGames; // +0x120  more_games.tex

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

    // --- Internal helpers ---
    // NOTE: no CreateToggles helper — the binary builds both sound/music toggles
    // INLINE at the top of Update @0x00196e1c, each under its own null guard.
    // v1.6.1 MainScreen::CreateButtons @0x001961f8: gated by flM_BombHitTimer<1.45, then per-button
    // null checks; sets m_ButtonsCreatedFlag=1 on first run. Called per-frame from case 0.
    void CreateButtons();
    void CreateQuitButton();
    void RemoveButton(MenuButton*& btn);

    // v1.6.1 MainScreen::DrawLoadingSymbol @ 0x00198fd4
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
static_assert(__builtin_offsetof(MainScreen, m_TexNewGame)     == 0x8c,  "m_TexNewGame offset");
static_assert(__builtin_offsetof(MainScreen, m_TexDojoIcon)    == 0x90,  "m_TexDojoIcon offset");
static_assert(__builtin_offsetof(MainScreen, m_TexOpenFeint)      == 0x94, "m_TexOpenFeint offset");
static_assert(__builtin_offsetof(MainScreen, m_TexGCAchievements) == 0x98, "m_TexGCAchievements offset");
static_assert(__builtin_offsetof(MainScreen, m_TexQuit)        == 0x9c,  "m_TexQuit offset");
static_assert(__builtin_offsetof(MainScreen, m_pGameModeButton) == 0xa0, "m_pGameModeButton offset");
static_assert(__builtin_offsetof(MainScreen, m_pStoreButton)    == 0xa4, "m_pStoreButton offset");
static_assert(__builtin_offsetof(MainScreen, m_pQuitButton)     == 0xa8, "m_pQuitButton offset");
static_assert(__builtin_offsetof(MainScreen, m_pMoreGamesBtn)   == 0xac, "m_pMoreGamesBtn offset");
static_assert(__builtin_offsetof(MainScreen, pToggleA)         == 0xb0,  "pToggleA offset");
static_assert(__builtin_offsetof(MainScreen, pSoundToggle)     == 0xb4,  "pSoundToggle offset");
static_assert(__builtin_offsetof(MainScreen, pMusicToggle)     == 0xb8,  "pMusicToggle offset");
static_assert(__builtin_offsetof(MainScreen, m_TexCommingSoon) == 0xbc,  "m_TexCommingSoon offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMusicOn)     == 0xcc,  "m_TexMusicOn offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMusicOff)    == 0xd0,  "m_TexMusicOff offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOn)     == 0xd4,  "m_TexSoundOn offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSoundOff)    == 0xd8,  "m_TexSoundOff offset");
static_assert(__builtin_offsetof(MainScreen, m_Model)          == 0xdc,  "m_Model offset");
static_assert(__builtin_offsetof(MainScreen, m_pSliceInstrBox) == 0xe0,  "m_pSliceInstrBox offset");
static_assert(__builtin_offsetof(MainScreen, m_TexSliceFruit)  == 0xe4,  "m_TexSliceFruit offset");
static_assert(__builtin_offsetof(MainScreen, m_LogoPos)        == 0xe8,  "m_LogoPos offset");
static_assert(__builtin_offsetof(MainScreen, m_Lean)           == 0xf4,  "m_Lean offset");
static_assert(__builtin_offsetof(MainScreen, m_NinjaTextX)     == 0xf8,  "m_NinjaTextX offset");
static_assert(__builtin_offsetof(MainScreen, m_BounceVel)      == 0x104, "m_BounceVel offset");
static_assert(__builtin_offsetof(MainScreen, m_BounceY)        == 0x108, "m_BounceY offset");
static_assert(__builtin_offsetof(MainScreen, m_StateTimer)     == 0x110, "m_StateTimer offset");
static_assert(__builtin_offsetof(MainScreen, m_Field114)       == 0x114, "m_Field114 offset");
static_assert(__builtin_offsetof(MainScreen, m_State)          == 0x118, "m_State offset");
static_assert(__builtin_offsetof(MainScreen, m_IntroHoldTimer) == 0x11c, "m_IntroHoldTimer offset");
static_assert(__builtin_offsetof(MainScreen, m_TexMoreGames)   == 0x120, "m_TexMoreGames offset");
static_assert(__builtin_offsetof(MainScreen, m_Timer2)         == 0x124, "m_Timer2 offset");
static_assert(__builtin_offsetof(MainScreen, m_pFont)          == 0x128, "m_pFont offset");
static_assert(sizeof(MainScreen)                               == 0x12c, "MainScreen size");
#endif

#endif
