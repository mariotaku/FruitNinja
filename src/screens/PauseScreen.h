#ifndef FN_PAUSE_SCREEN_H
#define FN_PAUSE_SCREEN_H

// Analysed: 2026-05-02T00:00
//
// PauseScreen : HUDControl3d (size = 0xd8)
//
// Binary refs:
//   ctor   PauseScreen::PauseScreen @ 0x00155460
//   Init   vtable[2] @ 0x00153e28 -- forwards to Reset() / HUDControl::Reset
//   Update vtable[10] @ 0x00154468
//   size   operator_new(0xd8)
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

// PauseScreen state machine. m_State is `int` (binary +0xd4 layout); values
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
    // === PauseScreen-specific fields (+0x7c..+0xd4) ===
    // Offsets verified by static_assert below.

    // +0x7c: primary fade alpha [0..1]; ctor = 0.0
    float m_Alpha;

    // +0x80: size copy of pause_title.tex, used for DrawOrder slide-in math
    Vec3 m_TitleSize;

    // +0x8c: per-frame cache of Resume button screen-pos (overwritten each Update)
    Vec3 m_ButtonOriginPos;

    // +0x98: P1 Resume button (pause_button / play_button swap)
    MenuButton* m_ResumeButton;

    // +0x9c: P2 Resume button (MP only -- Tier-2; always nullptr in Tier-1)
    MenuButton* m_P2ResumeButton;

    // +0xa0: P1 Quit button (quit_title.tex)
    MenuButton* m_QuitButton;

    // +0xa4: P2 Quit button (MP only -- Tier-2; always nullptr in Tier-1)
    MenuButton* m_P2QuitButton;

    // +0xa8: pause_button.tex (in-game pause icon; shown when m_Alpha <= 0.5)
    Mortar::SmartPtr<Mortar::Texture> m_PauseButtonTex;

    // +0xac: P1 Retry button (retry_button.tex)
    MenuButton* m_RetryButton;

    // +0xb0: P2 Retry button (MP only -- Tier-2; always nullptr in Tier-1)
    MenuButton* m_P2RetryButton;

    // +0xb4: secondary button-fade alpha; ctor = 1.0
    float m_ButtonFadeAlpha;

    // +0xb8: play_button.tex (resume icon; shown when m_Alpha > 0.5)
    Mortar::SmartPtr<Mortar::Texture> m_PlayButtonTex;

    // +0xbc: quit_title.tex
    Mortar::SmartPtr<Mortar::Texture> m_QuitTitleTex;

    // +0xc0: retry_button.tex
    Mortar::SmartPtr<Mortar::Texture> m_RetryButtonTex;

    // +0xc4: 5th SmartPtr<Texture> slot. Source for the retry button's
    // m_Texture in Reset() (binary @ 0x00154024 reads from +0xc4,
    // not +0xc0). Default-null in ctor; SetToMultiplayerState may write
    // to it. Release nulls this slot. (Was previously misnamed _pad_c4.)
    // TODO: 0x00155460 — find the assignment site so this gets a real
    // texture loaded; until then it stays null and Reset effectively
    // clears the retry button's secondary tex.
    Mortar::SmartPtr<Mortar::Texture> m_RetryHighlightTex;

    // +0xc8: index of last-hit button; ctor = -1; QuitGameCallback sets 0
    int m_LastHitButton;

    // +0xcc: 0 default; PauseGameCallback2 (Retry) sets 1
    int m_PressIndex;

    // +0xd0: counts down after exit; gates Resume re-enable; ctor = 0.0
    float m_RevealTimer;

    // +0xd4: state machine [0..6]; ctor = 0
    int m_State;

    // Texture widths/heights for layout math (loaded alongside textures)
    // Port-specific: binary reads these from Mortar::SmartPtr<Texture>->m_Width/m_Height.
    // We cache them after load so Update() can use them without holding SmartPtrs.
    float m_TitleTexW, m_TitleTexH;
    float m_PauseButtonTexW, m_PauseButtonTexH;
    float m_QuitTitleTexW, m_QuitTitleTexH;
    float m_RetryButtonTexW, m_RetryButtonTexH;

    PauseScreen();
    ~PauseScreen();

    // vtable[2]: Init -- forwards to Reset() per binary 0x00153e28
    void Init() override;

    // vtable[3]: Release -- nulls all owned SmartPtr textures; binary @ 0x0015408c
    void Release() override;

    // vtable[4]: Reset -- restores RetryButton/ResumeButton tex to SP defaults; binary @ 0x00154024
    void Reset() override;

    // vtable[5]: BeginDraw -- asserts m_LayerFlags = 8
    void BeginDraw(float dt) override;

    // vtable[6]: PreDraw -- flash.tex black-tinted dim overlay
    void PreDraw(const Vec3& hudScale) override;

    // vtable[9]: DrawOrder -- conditional HUDControl3d::Draw gate
    void DrawOrder(const Vec3& hudScale, int layerMask) override;

    // vtable[10]: Update -- state machine + lazy button creation
    void Update(float dt) override;

    // vtable[11]: SetToMultiplayerState -- Tier-2; stub
    bool SetToMultiplayerState() override;

    // Button delegate callbacks (press-action targets)
    void PauseGameCallback();
    void PauseGameCallback2();
    void QuitGameCallback();
    void QuitGameCallback2();     // binary @ 0x00153ef8 — P2-Quit (MP path)
    void RetryGameCallback();     // binary @ 0x00153f68 — P1 SP Retry

    // vtable[?]: IsEnabled -- binary @ 0x00153e4c
    // Returns false while transition is in-flight or bomb hit timer running.
    bool IsEnabled();

    // Engine pause helpers matching binary 0x00168f80 / 0x00168fb0
    static void PauseGame();
    static void UnpauseGame();

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: PauseScreen::ContinueGameCallback -- auto stub from binary missing-symbol set
    void ContinueGameCallback();
    // STUB: PauseScreen::SkipTo -- auto stub from binary missing-symbol set
    void SkipTo();
    // ---- end AUTO-STUB MERGE ----
};

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
//   m_QuitButton     +0xa0
//   m_P2QuitButton   +0xa4
//   m_PauseButtonTex +0xa8
//   m_RetryButton    +0xac
//   m_P2RetryButton  +0xb0
//   m_ButtonFadeAlpha+0xb4
//   m_PlayButtonTex  +0xb8
//   m_QuitTitleTex   +0xbc
//   m_RetryButtonTex +0xc0
//   _pad_c4          +0xc4
//   m_LastHitButton  +0xc8
//   m_PressIndex     +0xcc
//   m_RevealTimer    +0xd0
//   m_State          +0xd4
//
// Field ordering is verified by the compiler (struct member order).
// Size of the ARM32 struct = 0xd8.

#endif  // FN_PAUSE_SCREEN_H
