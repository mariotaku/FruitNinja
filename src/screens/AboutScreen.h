#ifndef FN_ABOUT_SCREEN_H
#define FN_ABOUT_SCREEN_H

//
// AboutScreen : HUDControl3d  (no BaseScreen, direct subclass)
//
// Binary refs:
//   Constructor      0x0012ecb8  (AboutScreen(DojoScreen*))
//   LoadContent      0x0012ec14  (static, loads 3 textures)
//   Destructor       0x0012eee0
//   Update           0x0012f020
//   Draw             0x0012f394
//
// Credits / about page shown as a child of DojoScreen.
// Pops up when DojoScreen::AboutCallback fires (state 3).
//
// Textures (static, loaded by LoadContent):
//   haikus.tex     -> s_TexHaiku   (background panel, assigned to base m_Texture in ctor)
//   credits.tex    -> s_TexCredits (slides up from bottom in Draw block 3)
//   sensei.tex     -> s_TexSensei  (slides in from right in Draw block 4)
//
// Binary layout (HUDControl3d base = 0x7C bytes):
//   +0x7C  float              m_TransitionAlpha   0->1 lerp / decay  (ctor=0.0f)
//   +0x80..+0x8B  [12 bytes reserved/padding — not written in ctor]
//   +0x8C  int32_t            m_pBackButton       back button ptr (init 0)
//   +0x90  DojoScreen*        m_pParent            parent back-navigation (ctor param_1)
//   +0x94  int32_t            m_pOFNButton         OFN/GameCenter btn slot (init 0, defunct)
//   +0x98  SmartPtr<Texture>  m_TexOFNOverlay      OFN overlay tex (null in port, defunct)
//   +0x9C  int32_t            m_State              0=in, 1=idle, 2=out (init 0)
//   Total size: 160 / 0xA0
//
// Note: haiku panel texture lives in the BASE HUDControl3d::m_Texture @ +0x74.
//       Ctor assigns base.m_Texture = s_TexHaiku. No per-instance TexHaiku member.
//
// State machine:
//   0: alpha lerp (+= (1-alpha)*0.125). When sensei tex loaded AND OFN button
//      not yet created: create OFN button at (480, 0, 0) (stub in port).
//      When alpha > 0.9990: create back button, advance to state 1.
//   1: idle
//   2: fade out (alpha *= 0.75). When alpha < 0.001:
//      call parent->Init() (restores DojoScreen), mark self pending-removal.
//

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
class DojoScreen;

class AboutScreen : public HUDControl3d {
public:
    // Matches AboutScreen::AboutScreen @ 0x0012ecb8
    // Binary ctor signature: AboutScreen(DojoScreen*) — no Game& parameter.
    AboutScreen(DojoScreen* parent);

    // Matches AboutScreen::~AboutScreen @ 0x0012eee0
    ~AboutScreen() override;

    // Matches AboutScreen::LoadContent @ 0x0012ec14
    // Loads haikus.tex, credits.tex, sensei.tex into static storage.
    // Called lazily from ctor if not yet loaded.
    static void LoadContent();

    // Matches AboutScreen::UnLoadContent (companion to LoadContent).
    // Releases static texture SmartPtrs so GL resources are freed on shutdown.
    static void UnLoadContent();

    // HUDControl overrides
    void Init()    override;
    void Release() override;
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;

    int GetType() override { return 1; }

    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    // Under __bada__ (incl. the asm-verify cross-build) the layout members are
    // public so the offsetof() static_asserts below can reach them — GCC 4.4.1
    // rejects offsetof on private members. On the host they stay private.
#if defined(__bada__)
public:
#else
private:
#endif
    // +0x7C in binary: transition scalar, ctor=0.0f
    float m_TransitionAlpha;

    // +0x80..+0x8B: 12 bytes not written in ctor (padding / reserved state)
    char  m_reserved[12];

    // +0x8C: back button ptr (created lazily when alpha crosses 0.9990)
    MenuButton* m_pBackButton;

    // +0x90: parent DojoScreen (ctor param_1)
    DojoScreen* m_pParent;

    // +0x94: OpenFeint/GameCenter button slot — defunct, init 0
    MenuButton* m_pOFNButton;

    // +0x98: OFN overlay texture SmartPtr (null in port — OFN defunct)
    Mortar::SmartPtr<Mortar::Texture> m_TexOFNOverlay;

    // +0x9C: state machine (0=in, 1=idle, 2=out)
    int m_State;

private:
    // Static textures (GOT-relative globals in binary, LoadContent manages them)
    static Mortar::SmartPtr<Mortar::Texture> s_TexHaiku;    // haikus.tex  (DAT_0012eca0 slot)
    static Mortar::SmartPtr<Mortar::Texture> s_TexCredits;  // credits.tex (DAT_0012eca8 slot)
    static Mortar::SmartPtr<Mortar::Texture> s_TexSensei;   // sensei.tex  (DAT_0012ecb0 slot)
    static Mortar::SmartPtr<Mortar::Texture> s_TexBackIcon; // back_icon.tex (port-only; binary reads game->field_0x17c)

    // One-time init guard (binary: DAT_0012ed94 + 0xc in BSS)
    static bool s_bContentLoaded;

    // Back button callback -> start fade-out (state 2)
    void BackCallback();

    // Helpers
    void CreateBackButton();
    void RemoveBackButton();

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: AboutScreen::QuitGameCallback -- auto stub from binary missing-symbol set
    void QuitGameCallback();
    // ---- end AUTO-STUB MERGE ----
};

#if defined(__bada__)
#include <cstddef>
struct AboutScreenLayoutAssert {
    static_assert(offsetof(AboutScreen, m_TransitionAlpha) == 0x7C, "m_TransitionAlpha offset");
    static_assert(offsetof(AboutScreen, m_pBackButton)     == 0x8C, "m_pBackButton offset");
    static_assert(offsetof(AboutScreen, m_pParent)         == 0x90, "m_pParent offset");
    static_assert(offsetof(AboutScreen, m_pOFNButton)      == 0x94, "m_pOFNButton offset");
    static_assert(offsetof(AboutScreen, m_TexOFNOverlay)   == 0x98, "m_TexOFNOverlay offset");
    static_assert(offsetof(AboutScreen, m_State)           == 0x9C, "m_State offset");
    static_assert(sizeof(AboutScreen)                      == 160,   "AboutScreen size");
};
#endif

#endif // FN_ABOUT_SCREEN_H
