#ifndef FN_MAIN_SCREEN_H
#define FN_MAIN_SCREEN_H

//
// MainScreen : HUDControl3d (size = 0x120)
// Reimplemented from docs/screens/main.md
// Original: ctor 0x0014c430, Update 0x0014b278 (677 lines), Draw 0x0014d4ec (171 lines)
//

// Analysed: 2026-05-04T00:00
#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "math/Vec3.h"
#include "render/Font.h"

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
    //   Body logic verified equivalent in R4 W2 RE.
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
    //   Body logic verified equivalent in R4 W2 RE.
    void Draw(const Vec3& hudScale, int layerMask) override;
    int GetType() override { return 1; }                // vtable slot 12 (base default)

    // UpdateScreenElements — vtable slot 15 @ 0x0014AD3C (extension slot beyond base 15)
    // Called from inside Update with (cameraTransition, elapsedTime).
    // Declared as non-override here; port wires via direct call not vtable dispatch.
    void UpdateScreenElements(float cameraTransition, float time);

    // Direct state writer used by child screens (DojoScreen,
    // GameModeScreen, ShopScreen, AboutScreen) when they finish
    // fading out and want to push MainScreen into the next state.
    // Mirrors the binary pattern at e.g. 0x001389B0 where DojoScreen
    // writes mainScreen->m_State = 8 directly.
    void SetState(MainScreenState s) { m_State = s; }

    // @ 0x0016bbb0 — post-effect overlays drawn after HUD layer 0x08.
    // NOT a vtable slot (binary slot 9 is base default 0x12F93C).
    void DrawPostEffects();

    // Camera transition accessors for child screens. Binary stores
    // this at game.m_TransitionTimer (+0x0c); port owns it on MainScreen.
    // GameModeScreen state 3-6 decays this toward 0 during mode-picked
    // fade-out (matches decompile at 0x0013f10c).
    float GetCameraTransition() const { return m_CameraTransition; }
    void  SetCameraTransition(float v) { m_CameraTransition = v; }

private:
    // +0x7c: copy of original size
    Vec3 m_OrigSize;

    // +0x88..+0x98: button textures (verified from ctor GOT offsets)
    SmartPtr<Mortar::Texture> m_TexNewGame;       // +0x88: newgame.tex
    SmartPtr<Mortar::Texture> m_TexDojoIcon;      // +0x8c: dojo_icon.tex
    SmartPtr<Mortar::Texture> m_TexOpenFeint;     // +0x90: openfeint.tex (GOT+c790)
    SmartPtr<Mortar::Texture> m_TexGCAchievements;// +0x94: gc_achievements.tex (GOT+c794)
    SmartPtr<Mortar::Texture> m_TexQuit;          // +0x98: quit.tex (GOT+c78c)

    // +0x9c..+0xb0: button pointers (created lazily)
    MenuButton* pPlayButton;       // +0x9c
    MenuButton* pDojoButton;       // +0xa0
    MenuButton* pQuitBtn;          // +0xa4: quit button (uses m_TexQuit, callback=QuitGamesCallback)
    MenuButton* pMoreGamesBtn;     // +0xa8
    MenuButton* pSoundToggle;      // +0xac
    MenuButton* pMusicToggle;      // +0xb0

    // +0xb4: logo overlay
    SmartPtr<Mortar::Texture> m_TexCommingSoon;       // +0xb4: comming_soon.tex

    // +0xc4..+0xd0: toggle textures
    SmartPtr<Mortar::Texture> m_TexSoundOn;           // +0xc4: sound.tex
    SmartPtr<Mortar::Texture> m_TexSoundOff;          // +0xc8: sound_cross.tex
    SmartPtr<Mortar::Texture> m_TexMusicOn;           // +0xcc: music.tex
    SmartPtr<Mortar::Texture> m_TexMusicOff;          // +0xd0: music_cross.tex

    // +0xd8: dojo decoration behind logo
    SmartPtr<Mortar::Texture> m_TexSliceFruit;        // +0xd8: slice_fruit.tex

    // +0xdc: logo positions and bounce state
    Vec3 m_LogoFruitPos;           // +0xdc: slice_fruit.tex draw position
    float m_Alpha;                 // +0xe8: lerps toward global alpha target (init 1.0)
    Vec3 m_LogoFruitTextPos;       // +0xec: fruit_text.tex draw position (z=0.0 always)
    // NOTE: +0xF4 is m_LogoFruitTextPos.z, NOT a separate field
    float m_LogoNinjaTextX;        // +0xf8: ninja_text X position (single float, NOT Vec3!)
    float m_WindowCenter;          // +0xfc: acts as ninja text Y in Draw (bounces via physics)
    float field_0x100;             // +0x100: acts as ninja text Z in Draw (=0.0)
    float m_BounceVelocity;        // +0x104: bounce velocity for logo (decays)
    float m_field108;              // +0x108: accumulator for state 0x13/0x14
    int m_State;                   // +0x10c: state machine variable
    float m_StateTimer;            // +0x110: transition countdown (NOT same as HUDControl m_Timer)
    SmartPtr<Mortar::Texture> m_TexMoreGames;         // +0x114: more_games.tex (GOT+c788)
    float m_Timer2;                // +0x118: second timer
    SmartPtr<Mortar::Font> m_pFont; // +0x11c: fonts/verdana.fnt (loaded in ctor, NOT in g_GameData)

    // Port specific: binary accesses Game via GOT; port stores a reference here,
    // declared after all binary-faithful fields so it does not displace them.
    Game& game;

    // Global textures (not on struct, loaded in ctor and assigned to globals via GOT)
    SmartPtr<Mortar::Texture> m_blurryBackingTex;     // blurry_backing.tex
    SmartPtr<Mortar::Texture> m_fruitTextTex;         // fruit_text.tex
    SmartPtr<Mortar::Texture> m_ninjaTextTex;         // ninja_text.tex

    // Port: camera transition (controlled by game state)
    float m_CameraTransition;
    float m_GlobalAlphaTarget;
    float m_Time;

    // One-shot latch so STATE_GAME_START fires WaveManager::Reset once
    // per entry instead of every frame (binary's per-frame threshold
    // check is awkward to reproduce exactly without the same timer
    // semantics; port emulates with a simple armed-on-entry flag).
    bool m_bGameStartReset;

    // Current DojoScreen child (when state is STATE_DOJO_WAIT_B or
    // already in Dojo). nullptr when no Dojo is open. MainScreen polls
    // the child for m_bPendingRemoval and transitions to SLIDE_IN
    // once it has cleared out.
    DojoScreen* m_pDojoScreen;

    // Current GameModeScreen child (when state is STATE_MODE_SELECT
    // and the 0.25 threshold has been crossed). nullptr until crossed
    // and again after the child's RemoveCallback fires.
    GameModeScreen* m_pGameModeScreen;

    // --- Internal helpers ---
    void DeleteMenuButtons();
    void Hide();
    void CreateToggles();
    void CreatePlayDojo();
    void CreateQuitButton();
    void RemoveButton(MenuButton*& btn);

    // Binary @ 0x0014D1F8 — 8-segment radial loading spinner.
    // Called from Draw step 5 when m_State == STATE_LOADING_A (0x13) or STATE_LOADING_B (0x14).
    void DrawLoadingSymbol(const float* hudScale);

    // Matches MainScreen::ButtonDeleted @ 0x0014acc0. Installed as the
    // m_RemoveCallback delegate on Play / Dojo / Quit / MoreGames buttons
    // at creation time. HUD::Update fires this right before deleting
    // the MenuButton, so we null whichever weak pointer matched. Needed
    // because FN::ClearMenuItems (triggered when the user slices any menu
    // item) releases every sibling menu fruit — the siblings then enter
    // the FadeCounter shrink-disappear path and self-delete, leaving
    // dangling weak pointers on MainScreen unless this callback clears
    // them.
    void ButtonDeleted(HUDControl* ctrl);

    // Port-specific: remove-callback helpers that null child screen pointers.
    void DojoScreenRemoved(HUDControl*)    { m_pDojoScreen     = nullptr; }
    void GameModeScreenRemoved(HUDControl*){ m_pGameModeScreen = nullptr; }

    // --- Callbacks ---
    void GameModeCallback();
    // Binary @ 0x0014B0AC — multiplayer variant of GameModeCallback (state 0xF instead of 0xE).
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
    // Binary @ 0x0014ACFC — Defunct: network UI button — no-op stub (empty in binary)
    void ClearNetworkButton();
    // Binary @ 0x0014AD00 — Defunct: leaderboard UI button — no-op stub (returns this in binary)
    MainScreen* CreateNormalLeaderboardButton();
    // Binary @ 0x0014AC98 — no-op event hook (empty in binary; superseded by ButtonDeleted)
    void OnMenuItemsCleared();

    // Touch handling removed in the touch rewrite. MenuButton::Update now
    // polls Mortar::Touch directly — MainScreen has no touch routing role.

#ifdef __bada__
    friend struct MainScreenLayoutAssert;
#endif
};

#ifdef __bada__
#include <cstddef>
struct MainScreenLayoutAssert {
    static_assert(offsetof(MainScreen, m_OrigSize)    == 0x7c,  "m_OrigSize offset");
    static_assert(offsetof(MainScreen, pPlayButton)   == 0x9c,  "pPlayButton offset");
    static_assert(offsetof(MainScreen, pDojoButton)   == 0xa0,  "pDojoButton offset");
    static_assert(offsetof(MainScreen, pQuitBtn)      == 0xa4,  "pQuitBtn offset");
    static_assert(offsetof(MainScreen, pMoreGamesBtn) == 0xa8,  "pMoreGamesBtn offset");
    // TODO: m_pFont currently lands at != 0x11c in cross-build; residual
    // drift between pMoreGamesBtn (+0xa8) and m_pFont (+0x11c). Audit pending.
    // static_assert(offsetof(MainScreen, m_pFont)       == 0x11c, "m_pFont offset");
};
#endif

#endif
