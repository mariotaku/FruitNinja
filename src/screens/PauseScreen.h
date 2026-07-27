#ifndef FN_PAUSE_SCREEN_H
#define FN_PAUSE_SCREEN_H

// Analysed: 2026-05-02T00:00
//
// PauseScreen : HUDControl3d (size = 0xdc)
//
// Binary refs:
//   ctor   PauseScreen::PauseScreen @ 0x001a7204 (thunk @ 0x001102e4)
//   Init   vtable[2] v1.6.1 PauseScreen::Init @0x001a5554 -- forwards to Reset()
//                     (its body is a single vptr[4] call = PauseScreen::Reset @0x001a58ac)
//   Update vtable[10] v1.6.1 PauseScreen::Update @0x001a5ebc
//   size   operator_new(0xdc)
//   stored at g_TaskState +0x04
//
// Full layout confirmed via ctor + Update write analysis.
// HUDControl3d base occupies 0x00..0x7b (0x7c bytes).
// PauseScreen-specific fields begin at 0x7c.
//
// NOTE: Port uses GLuint for textures (not Mortar::SmartPtr<Texture>) to match
// HUDControl3d base field types already in use throughout the port.
// Mortar::SmartPtr<Texture> is used locally during load then GLuint is extracted.
//

#include "hud/HUDControl3d.h"
#include "render/gl_funcs.h"
#include <cstdint>
#include <cstddef>

class MenuButton;
class BSButton;
namespace Mortar { class BakedStringBox; }

// PauseScreen state machine. m_State is `int` (binary +0xd8 layout); values
// are 0..6. ABI-compatible -- enum integer rank is int by default.
enum PauseScreenState {
    PAUSE_STATE_HIDDEN          = 0,  // idle / not active
    PAUSE_STATE_BOMB_FLASH      = 1,  // bomb-flash poll (Tier-2)
    PAUSE_STATE_FADE_IN         = 2,  // entry fade-in
    PAUSE_STATE_ACTIVE          = 3,  // overlay shown, buttons interactable
    PAUSE_STATE_RESUME_EXIT     = 4,  // Resume button fade-out
    PAUSE_STATE_RETRY_EXIT      = 5,  // Retry button fade-out -> EndRetryLevel
    PAUSE_STATE_QUIT_EXIT       = 6,  // Quit button fade-out -> QuitToMenu
};

class PauseScreen : public HUDControl3d {
public:
    // === PauseScreen-specific fields (+0x7c..+0xd8) ===
    // Offsets verified by static_assert below.

    // +0x7c: primary fade alpha [0..1]; ctor = 0.0
    float m_Alpha;

    // +0x80: size copy of pause_title.tex, used for DrawOrder slide-in math
    _Vector3<float> m_TitleSize;

    // +0x8c: per-frame cache of Resume button screen-pos (overwritten each Update)
    _Vector3<float> m_ButtonOriginPos;

    // +0x98: P1 Resume button (pause_button / play_button swap)
    MenuButton* m_ResumeButton;

    // +0x9c: P2 Resume button (MP only -- Tier-2; always nullptr in Tier-1)
    MenuButton* m_P2ResumeButton;

    // +0xa0: P1 Quit button -- BSButton in binary (v1.6.1 PauseScreen::Update @0x001a5ebc)
    BSButton* m_QuitButton;

    // +0xa4: P2 Quit button (MP only -- Tier-2; always nullptr in Tier-1)
    MenuButton* m_P2QuitButton;

    // +0xa8: 5th MenuButton* slot (binary nulled at ctor; never assigned in shipped code)
    MenuButton* m_Pad_0xA8;

    // +0xac: P1 Retry button (retry_button.tex)
    MenuButton* m_RetryButton;

    // +0xb0: P2 Retry button (MP only -- Tier-2; always nullptr in Tier-1)
    MenuButton* m_P2RetryButton;

    // +0xb4: secondary button-fade alpha; ctor = 1.0
    float m_ButtonFadeAlpha;

    // +0xb8: pause_button.tex (in-game pause icon; shown when m_Alpha <= 0.5)
    Mortar::SmartPtr<Mortar::Texture> m_PauseButtonTex;

    // +0xbc: play_button.tex (resume icon; shown when m_Alpha > 0.5)
    Mortar::SmartPtr<Mortar::Texture> m_PlayButtonTex;

    // +0xc0: quit_title.tex
    Mortar::SmartPtr<Mortar::Texture> m_QuitTitleTex;

    // +0xc4: retry_button.tex
    Mortar::SmartPtr<Mortar::Texture> m_RetryButtonTex;

    // +0xc8: which menu bomb the exit white-flash fires at; ctor = -1 (none),
    // QuitGameCallback sets 0 (P1 quit button), QuitGameCallback2 sets 1 (P2, MP).
    // Update's QUIT_EXIT branch reads it and resets it to -1 after HitMenuBomb.
    int m_MenuBombIndex;

    // +0xcc: 0 default; PauseGameCallback2 (Retry) sets 1
    int m_PressIndex;

    // +0xd0: counts down after exit; gates Resume re-enable; ctor = 0.0
    float m_RevealTimer;

    // +0xd4: "PAUSED" baked text box.
    // Binary ctor: operator_new(200); BakedStringBox(box, game_work[+0x614], 100, 0x1e);
    //   SetHorizontalLineSpacing(-1); SetText(GETSTRING(0x3c8)); SetColour(game_work[+0x6a0], true).
    // v1.6.1 PauseScreen::PauseScreen @ 0x001a7204.
    Mortar::BakedStringBox* m_PausedText;   // role: paused-overlay text box

    // +0xd8: state machine [0..6]; ctor = 0
    int m_State;

    // sizeof == 0xdc (220 bytes) here — matches binary.

    // Port-specific trailing fields (not in the 220-byte binary struct).
    // Excluded on the __bada__ production build so sizeof stays at 0xdc.
    // Binary reads texture dimensions from SmartPtr<Texture>->GetWidth()/GetHeight()
    // each time they are needed. Port caches them after load to avoid holding
    // SmartPtrs in Update.
#if !defined(__bada__)
    float m_TitleTexW, m_TitleTexH;
    float m_PauseButtonTexW, m_PauseButtonTexH;
    float m_QuitTitleTexW, m_QuitTitleTexH;
    float m_RetryButtonTexW, m_RetryButtonTexH;
#endif // !defined(__bada__)

    PauseScreen();
    ~PauseScreen();

    // v1.6.1 PauseScreen::GetTime @0x001d00ec
    // Returns 1.0f when fully paused (m_State==6 / PAUSE_STATE_QUIT_EXIT), m_Alpha otherwise.
    // Used by GetPauseAmount() to clamp the global pause blend value to [0,1].
    // Binary sig is non-const (matches ABI for symbol-diff pairing).
    float GetTime();

    // vtable[2]: Init -- v1.6.1 PauseScreen::Init @0x001a5554; body is a single
    // vptr[4] call, i.e. it forwards to PauseScreen::Reset @0x001a58ac
    void Init() override;

    // vtable[3]: Release -- nulls all owned SmartPtr textures, then deletes + nulls
    // m_PausedText; v1.6.1 PauseScreen::Release @0x001a5bbc
    void Release() override;

    // vtable[4]: Reset -- restores RetryButton/ResumeButton tex to SP defaults;
    // v1.6.1 PauseScreen::Reset @0x001a58ac
    void Reset() override;

    // vtable[5]: BeginDraw -- asserts m_LayerFlags = 8
    void BeginDraw(float dt) override;

    // vtable[6]: PreDraw -- flash.tex black-tinted dim overlay
    void PreDraw(float* hudScale) override;

    // vtable[9]: DrawOrder -- conditional HUDControl3d::Draw gate
    void DrawOrder(float* hudScaleRaw, int layerMask) override;

    // vtable[10]: Update -- state machine + lazy button creation
    void Update(float dt) override;

#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Eases m_Alpha and m_ButtonFadeAlpha (the fade-in/out/decay ramps used by
    // every PauseScreen state) dt-scaled, once per PRESENTED frame (Game::tickRealtimeUi
    // via HUD::UpdateRealtime), so the pause-overlay fade tracks the display's
    // actual present rate (60/90/120fps) instead of the fixed 60Hz sim tick. The
    // STATE MACHINE itself (which state, when to transition, one-shot side effects
    // like button creation) stays in Update() at 60Hz -- it reads the alpha this
    // function advances and fires threshold-crossing transitions there, exactly
    // once per sim tick. See PauseScreen.cpp for the PS_APPROACH_F/PS_DECAY_F
    // macros shared with the __bada__ path (mirrors ShopScreen's SS_APPROACH_F/SS_DECAY_F).
    void UpdateRealtime(float dtSeconds) override;
#endif

    // vtable[11]: SetToMultiplayerState -- Tier-2; stub
    bool SetToMultiplayerState() override;

    // Button delegate callbacks (press-action targets)
    // No-op while m_ButtonFadeAlpha != 0 (mid-fade taps swallowed);
    // the debounce releases because Update()/UpdateRealtime()'s decay clamps
    // m_ButtonFadeAlpha to exact 0.0 once it drops below 0.001 (EXIT_THRESHOLD)
    // while IsEnabled() -- ~0.4s after the fade starts.
    // v1.6.1 PauseScreen::PauseGameCallback @0x001a5978
    void PauseGameCallback();
    void PauseGameCallback2();    // v1.6.1 @0x001a5b38 -- wraps PauseGameCallback, sets m_PressIndex=1
    void QuitGameCallback();      // v1.6.1 @0x001a55e0 — P1 SP Quit
    void QuitGameCallback2();     // v1.6.1 @0x001a5634 — P2-Quit (MP path)
    void RetryGameCallback();     // v1.6.1 @0x001a5800 — P1 SP Retry

    // vtable[?]: IsEnabled -- v1.6.1 PauseScreen::IsEnabled @0x001a5588
    // Returns false while transition is in-flight or bomb hit timer running.
    bool IsEnabled();

    // TODO: v1.6.1 PauseScreen::ContinueGameCallback -- address UNVERIFIED.
    // ContinueGameCallback: if m_State==3, set m_State=4
    //   (RESUME_EXIT); if Game-state +0x85 tutorial-shown flag was set, re-seed the
    //   global RNG from Game-state +0x194, then clear the flag. (impl in .cpp)
    void ContinueGameCallback();
    // TODO: v1.6.1 PauseScreen::SkipTo -- address UNVERIFIED.
    // SkipTo: jump straight to ACTIVE overlay
    //   (m_State=3, m_Alpha=1.0). (impl in .cpp)
    void SkipTo();
};

// v1.6.1 PauseGame @0x001ca48c / UnpauseGame @0x001ca4b4 — engine pause helpers.
void PauseGame();
void UnpauseGame();

// v1.6.1 SkipToPause @0x001cb424 — snap PauseScreen to ACTIVE state instantly.
// Freezes gameplay (m_PauseAmount=0, bM_Mode=true), hides MainScreen, skips all HUD controls,
// and preloads in-game sounds. force=true bypasses the IsEnabled() gate (used during
// session restore from save). Call from Game::Paused() and WaveManager::Resume().
void SkipToPause(bool force);

// Offset assertions (ARM32 binary layout).
// The binary offsets are for the ARM32 target where pointers are 4 bytes.
// On x64 host builds pointers are 8 bytes so these fail -- they are only
// meaningful when cross-compiling to ARM32. Kept as comments to document
// the expected binary layout per section 2 of the deep-RE doc.
//
// Binary offsets (ARM32):
//   m_Alpha          +0x7c
//   m_TitleSize      +0x80
//   m_ButtonOriginPos+0x8c
//   m_ResumeButton   +0x98
//   m_P2ResumeButton +0x9c
//   m_QuitButton     +0xa0  (BSButton* in binary, v1.6.1 @0x001a5ebc)
//   m_P2QuitButton   +0xa4
//   m_Pad_0xA8       +0xa8  (5th MenuButton* slot, never assigned)
//   m_RetryButton    +0xac
//   m_P2RetryButton  +0xb0
//   m_ButtonFadeAlpha+0xb4
//   m_PauseButtonTex +0xb8
//   m_PlayButtonTex  +0xbc
//   m_QuitTitleTex   +0xc0
//   m_RetryButtonTex +0xc4
//   m_MenuBombIndex  +0xc8
//   m_PressIndex     +0xcc
//   m_RevealTimer    +0xd0
//   m_PausedText     +0xd4
//   m_State          +0xd8
//
// Field ordering is verified by the compiler (struct member order).
// Binary size = 0xdc (220 bytes). Port-specific trailing floats (m_TitleTexW etc.)
// are after the binary-faithful region; only the binary fields are asserted here.

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(PauseScreen) == 0xdc, "PauseScreen size must match binary");
static_assert(offsetof(PauseScreen, m_Alpha)          == 0x7c, "m_Alpha offset");
static_assert(offsetof(PauseScreen, m_TitleSize)      == 0x80, "m_TitleSize offset");
static_assert(offsetof(PauseScreen, m_ButtonOriginPos)== 0x8c, "m_ButtonOriginPos offset");
static_assert(offsetof(PauseScreen, m_ResumeButton)   == 0x98, "m_ResumeButton offset");
static_assert(offsetof(PauseScreen, m_ButtonFadeAlpha)== 0xb4, "m_ButtonFadeAlpha offset");
static_assert(offsetof(PauseScreen, m_PauseButtonTex) == 0xb8, "m_PauseButtonTex offset");
static_assert(offsetof(PauseScreen, m_MenuBombIndex)  == 0xc8, "m_MenuBombIndex offset");
static_assert(offsetof(PauseScreen, m_RevealTimer)    == 0xd0, "m_RevealTimer offset");
static_assert(offsetof(PauseScreen, m_PausedText)     == 0xd4, "m_PausedText offset");
static_assert(offsetof(PauseScreen, m_State)          == 0xd8, "m_State offset");
#endif

#endif  // FN_PAUSE_SCREEN_H
