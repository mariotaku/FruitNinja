#ifndef FN_ABOUT_SCREEN_H
#define FN_ABOUT_SCREEN_H

//
// AboutScreen : HUDControl3d  (no BaseScreen, direct subclass)
//
// v1.6.1 binary refs:
//   Constructor           0x0015b764  (AboutScreen(DojoScreen*))
//   LoadContent           0x0015a548  (static, loads textures)
//   UnLoadContent         0x0015c2c0  (static, releases texture SmartPtrs)
//   Draw                  0x0015a654  (board panel + credits quad + sensei quads)
//   NewDraw               0x0015a264  (BakedStringBox credit text pass)
//   CreateCreditsMarquee  0x0015ac0c  (builds m_Marquees scrolling credits list)
//   AddLine               0x0015aaf0  (allocates one MarqueeText + BakedStringBox)
//   DrawMarquee           0x0015a138  (draws m_Marquees + heading box, called from NewDraw)
//   Update                0x0015c350  (state machine + marquee scroll)
//
// Credits / about page shown as a child of DojoScreen.
// Pops up when DojoScreen::AboutCallback fires (state 3).
//
// Textures (static, loaded by LoadContent):
//   haikus.tex     -> s_TexHaiku   (background panel, assigned to base m_Texture in ctor)
//   credits.tex    -> s_TexCredits (lower sliding credits plate)
//   sensei.tex     -> s_TexSensei  (slides in from right in Draw block D)
//
// Binary layout (HUDControl3d base = 0x7C bytes):
//   +0x7C  float              m_TransitionAlpha   0->1 lerp / decay  (ctor=0.0f)
//   +0x80..+0x8B  [12 bytes reserved/padding]
//   +0x8C  MenuButton*        m_pBackButton       back button ptr (init 0)
//   +0x90  DojoScreen*        m_pParent           parent back-navigation (ctor param_1)
//   +0x94  MenuButton*        m_pOFNButton        OFN/GameCenter btn slot (init 0, defunct)
//   +0x98  SmartPtr<Texture>  m_TexOFNOverlay     OFN overlay tex (null in port, defunct)
//   +0x9C  int32_t            m_State             0=in, 1=idle, 2=out (init 0)
//   // v1.6.1 additions: BakedStringBox* members (AboutScreen ctor @0x0015b764)
//   +0xA0  BakedStringBox*    m_TitleBox          LSTR 0x3c3
//   +0xA4  BakedStringBox*    m_HeadingBox        LSTR 0x349 (positioned by DrawMarquee)
//   +0xA8  BakedStringBox*    m_VersionBox        "V 1.6.1"
//   +0xAC  BakedStringBox*    m_CreditLine0       LSTR 0x34b
//   +0xB0  BakedStringBox*    m_CreditLine1       LSTR 0x34c
//   +0xB4  BakedStringBox*    m_CreditLine2       LSTR 0x34d
//   +0xB8  BakedStringBox*    m_CreditLine3       LSTR 0x34e
//   +0xBC  BakedStringBox*    m_CreditLine4       LSTR 0x34f
//   +0xC0  BakedStringBox*    m_CreditLine5       LSTR 0x350
//   // v1.6.1 marquee members (CreateCreditsMarquee @0x0015ac0c):
//   +0xC4  std::vector<MarqueeText*>  m_Marquees  (12 bytes on ARM32)
//   +0xD0  float              m_EntryDelay         scroll delay countdown (ctor=3.0f)
//
// State machine:
//   0: alpha lerp (+= (1-alpha)*0.125).
//      When alpha > 0.9990: create back button, advance to state 1.
//   1: idle
//   2: fade out (alpha *= 0.75). When alpha < 0.001:
//      call parent->Reset(), mark self pending-removal.
//

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <vector>

namespace Mortar { class BakedStringBox; }

class MenuButton;
class DojoScreen;

// MarqueeText -- scrolling credit list entry (0x10 bytes).
// ASM-spec v1.6.1 AboutScreen::CreateCreditsMarquee @0x0015ac0c / AddLine @0x0015aaf0:
//   +0x00  Vec3                pos         (x=layout, y=scroll accumulator, z=0)
//   +0x0C  Mortar::BakedStringBox*  m_pBox
struct MarqueeText {
    _Vector3<float> pos;    // +0x00
    Mortar::BakedStringBox*    m_pBox; // +0x0C
    MarqueeText() : pos(0.0f, 0.0f, 0.0f), m_pBox(0) {}
};

class AboutScreen : public HUDControl3d {
public:
    // v1.6.1 AboutScreen::AboutScreen @ 0x0015b764
    // Binary ctor signature: AboutScreen(DojoScreen*) — no Game& parameter.
    AboutScreen(DojoScreen* parent);

    ~AboutScreen() override;

    // v1.6.1 AboutScreen::LoadContent @ 0x0015a548
    // Loads haikus.tex, credits.tex, sensei.tex into static storage.
    // Called from ctor; binary has no early-return guard.
    static void LoadContent();

    // Matches AboutScreen::UnLoadContent (companion to LoadContent).
    // Releases static texture SmartPtrs so GL resources are freed on shutdown.
    static void UnLoadContent();

    // HUDControl overrides
    void Init()    override;
    void Release() override;
    void Update(float dt) override;
    void Draw(float* hudScaleRaw) override;

#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Eases m_TransitionAlpha (states 0/2) and scrolls the marquee dt-scaled,
    // once per PRESENTED frame (Game::tickRealtimeUi via HUD::UpdateRealtime),
    // so the fade and credits scroll track display refresh instead of the
    // fixed 60Hz sim tick. Update() (60Hz) still owns the state machine
    // (which state, when to transition, one-shot side effects like
    // CreateBackButton/m_pParent->Reset()) -- it reads the alpha this
    // function advances and fires threshold-crossing transitions there,
    // exactly once per sim tick. See AboutScreen.cpp for the AS_APPROACH_F/
    // AS_DECAY_F macros shared with the __bada__ path (mirrors ShopScreen's
    // SS_APPROACH_F/SS_DECAY_F).
    void UpdateRealtime(float dtSeconds) override;
#endif

    int GetType() override { return 1; }

    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

private:
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

    // +0xA0..+0xC0: BakedStringBox* members (v1.6.1, AboutScreen ctor @0x0015b764)
    // ASM-spec v1.6.1 AboutScreen::AboutScreen @0x0015b764: nine BakedStringBox
    // ptrs allocated in ctor; drawn by NewDraw @0x0015a264.
    Mortar::BakedStringBox* m_TitleBox;     // +0xA0: LSTR 0x3c3 (LSTR_ABOUT_TITLE)
    Mortar::BakedStringBox* m_HeadingBox;   // +0xA4: LSTR 0x349 (LSTR_ABOUT_HEADING), positioned by DrawMarquee
    Mortar::BakedStringBox* m_VersionBox;   // +0xA8: "V 1.6.1" (sprintf'd)
    Mortar::BakedStringBox* m_CreditLine0;  // +0xAC: LSTR 0x34b (LSTR_ABOUT_CREDIT0)
    Mortar::BakedStringBox* m_CreditLine1;  // +0xB0: LSTR 0x34c (LSTR_ABOUT_CREDIT1)
    Mortar::BakedStringBox* m_CreditLine2;  // +0xB4: LSTR 0x34d (LSTR_ABOUT_CREDIT2)
    Mortar::BakedStringBox* m_CreditLine3;  // +0xB8: LSTR 0x34e (LSTR_ABOUT_CREDIT3)
    Mortar::BakedStringBox* m_CreditLine4;  // +0xBC: LSTR 0x34f (LSTR_ABOUT_CREDIT4)
    Mortar::BakedStringBox* m_CreditLine5;  // +0xC0: LSTR 0x350 (LSTR_ABOUT_CREDIT5)
    // ASM-spec v1.6.1 AboutScreen ctor @0x0015b764 / CreateCreditsMarquee @0x0015ac0c:
    std::vector<MarqueeText*> m_Marquees;   // +0xC4: scrolling credits list (12B on ARM32)
    float m_EntryDelay;                     // +0xD0: scroll start delay countdown (ctor=3.0f)

#ifndef __bada__
    // Port specific: bridge field, no binary counterpart. Update() (60Hz)
    // owns the m_EntryDelay countdown and sets this true the tick m_EntryDelay
    // first reaches <= 0 (matching the binary's early-return gate); once set,
    // UpdateRealtime() scrolls the marquee dt-scaled on every subsequent
    // present. Placed after every offset-asserted field / the last
    // static_assert below so __bada__ sizeof(AboutScreen) and field layout
    // are completely unaffected -- this member does not exist at all under
    // __bada__.
    bool m_bMarqueeActive;
#endif

private:
    // Static textures (GOT-relative globals in binary, LoadContent manages them)
    static Mortar::SmartPtr<Mortar::Texture> s_TexHaiku;    // haikus.tex (binary: s_boardTexture)
    static Mortar::SmartPtr<Mortar::Texture> s_TexCredits;  // credits.tex (binary: m_creditsTexture)
    static Mortar::SmartPtr<Mortar::Texture> s_TexSensei;   // sensei.tex (binary: m_senseiTexture)
    // Defunct: Bada dual-SIM preferred-network button icon; v1.6.1 s_switchNetworkTexture,
    // gated in AboutScreen::Update @0x0015c428 via AskUserToChoosePreferredNetwork;
    // never set by LoadContent, only cleared here -- stays null on port.
    static Mortar::SmartPtr<Mortar::Texture> s_TexNetworkSwitch;

    // Helpers
    void CreateBackButton();

    // v1.6.1: NewDraw -- BakedStringBox credit text pass
    // ASM-spec v1.6.1 AboutScreen::NewDraw @0x0015a264: draws, in this order,
    // DrawMarquee (gated on alpha > 0.6, so the marquee lands UNDER the credits),
    // m_TitleBox, m_CreditLine0..5, m_VersionBox -- all at positions derived from
    // panelPos. m_HeadingBox is positioned inside DrawMarquee only.
    // Binary reads m_Texture->GetHeight() (+0x28) to compute panelBaseY internally
    // (no param -- corrected from port's erroneous float yDrawn param). Draw()
    // derives its yStart from the same GetHeight, so both track one Y curve.
    void NewDraw();

    // v1.6.1: CreateCreditsMarquee @0x0015ac0c
    // Builds m_Marquees by calling AddLine per credit entry, then lays out positions.
    void CreateCreditsMarquee();

    // v1.6.1: AddLine @0x0015aaf0
    // Allocates a BakedStringBox, sets text/colour/clip/updates, wraps in a MarqueeText.
    // Binary sig: AddLine(char const*, Colour, int) -- Colour passed by value (matches
    // binary ABI; ABI-identical to const& on ARM32). 3rd param is int; cast to float
    // internally before passing to BakedStringBox ctor (VectorSignedToFloat in binary).
    void AddLine(const char* text, Colour colour, int fontSize);

    // v1.6.1: DrawMarquee @0x0015a138
    // Draws each m_Marquees item translated by the transition offset, plus the
    // heading box rotated 90 degrees.
    void DrawMarquee();

public:
    // v1.6.1 AboutScreen::QuitGameCallback @ 0x0015c914.
    // Bound to m_pBackButton. Plays menu-bomb SFX, sets state 2, flits the
    // back-bomb so the ActorManager reaps it during fade-out, freeing the
    // menu-bomb pool for DojoScreen::Reset on re-entry.
    void QuitGameCallback();

#ifdef __bada__
    friend struct AboutScreenLayoutAssert;
#endif

#ifdef FN_TEST
public:
    // Test-only accessors (excluded from production/cross-build by FN_TEST guard).
    // Returns number of non-null BakedStringBox credit-text controls.
    // Expected 9 when TTF font initialises correctly (title+heading+version+6 credits).
    int GetBuiltBoxCount() const {
        int n = 0;
        if (m_TitleBox)    ++n;
        if (m_HeadingBox)  ++n;
        if (m_VersionBox)  ++n;
        if (m_CreditLine0) ++n;
        if (m_CreditLine1) ++n;
        if (m_CreditLine2) ++n;
        if (m_CreditLine3) ++n;
        if (m_CreditLine4) ++n;
        if (m_CreditLine5) ++n;
        return n;
    }
    // Returns number of scrolling marquee lines added by CreateCreditsMarquee.
    // Expected >= 10 (heading + dev names + colour-leader lines).
    int GetMarqueeCount() const { return (int)m_Marquees.size(); }
#endif // FN_TEST
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
    static_assert(offsetof(AboutScreen, m_TitleBox)        == 0xA0, "m_TitleBox offset");
    static_assert(offsetof(AboutScreen, m_HeadingBox)      == 0xA4, "m_HeadingBox offset");
    static_assert(offsetof(AboutScreen, m_VersionBox)      == 0xA8, "m_VersionBox offset");
    static_assert(offsetof(AboutScreen, m_CreditLine0)     == 0xAC, "m_CreditLine0 offset");
    static_assert(offsetof(AboutScreen, m_CreditLine5)     == 0xC0, "m_CreditLine5 offset");
    static_assert(offsetof(AboutScreen, m_Marquees)        == 0xC4, "m_Marquees offset");
    static_assert(offsetof(AboutScreen, m_EntryDelay)      == 0xD0, "m_EntryDelay offset");
    // sizeof(AboutScreen): v1.6.1 not yet RE'd; assert omitted.
};
#endif

#endif // FN_ABOUT_SCREEN_H
