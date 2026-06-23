#ifndef FN_DOJO_SCREEN_H
#define FN_DOJO_SCREEN_H

//
// DojoScreen : HUDControl3d (BaseScreen subclass, binary sizeof 0xb8)
//
// Binary refs (v1.6.1):
//   Constructor 0x0016bad8
//   Update      0x0016b6a4
//   Draw        0x0016a004
//   Init        0x00169e80
//   Release     0x0016c7f8
//   Reset       0x0016b568
//
// Secondary menu shown after tapping the Dojo button on MainScreen.
// Has sub-buttons: Back (return to game), Shop (blade/power-up shop),
// About (credits), plus defunct social-share buttons (BSButton).
//
// State machine:
//   0 = transition-in: lerp m_TransitionAlpha -> 1.0 (step 0.25),
//       create buttons, -> state 1
//   1 = idle
//   2 = fade out, -> ShopScreen  (port: stub, falls through to 6)
//   3 = fade out, -> AboutScreen
//   6 = fade out, pending removal -> MainScreen STATE_SLIDE_IN
//
// Port specific:
//   - No sensei 3D animation (binary has an animated 3D model).
//   - Shop button stub -- returns to MainScreen instead of opening ShopScreen.
//   - No "new item" badge (needs ItemManager).
//

#include "BaseScreen.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
class AboutScreen;
struct Game;

class DojoScreen : public BaseScreen {
public:
    DojoScreen(Game& g);
    ~DojoScreen();

    // HUDControl overrides
    void Init() override;
    void Release() override;
    void Reset() override;
    void Update(float dt) override;
    void Draw(float* hudScaleRaw) override;
    int  GetType() override { return 1; }

    // Binary stores textures at GOT-relative static storage.
    static void LoadContent();    // 0x00137a20
    static void UnLoadContent();  // 0x00137c04

    // True when the screen has completed its fade-out and wants to
    // be removed from the HUD. MainScreen polls this to transition
    // back to STATE_SLIDE_IN.
    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    // Matches DojoScreen::ButtonDeleted @ 0x0016bad8 region.
    // Remove callback for the shop button (field_0x98). Binary only
    // installs this on the shop button, not play or about.
    void ButtonDeleted(HUDControl* ctrl);

    // Port-specific: remove callback helper for AboutScreen child pointer.
    void AboutScreenRemoved(HUDControl*) { m_pAboutScreen = nullptr; }

private:
    // Binary own fields (BaseScreen base = 0x94; own fields 0x94..0xb4):
    MenuButton* m_pBackButton;   // +0x94 (binary name; port used m_pPlayButton)
    MenuButton* m_pShopButton;   // +0x98
    MenuButton* m_pAboutButton;  // +0x9c
    // Defunct: Twitter/Facebook social share -- stub; v1.6.1 DojoScreen ctor @0x0016bad8
    void*       m_pBSButton0;    // +0xa0 (BSButton* Facebook social share)
    // Defunct: Twitter/Facebook social share -- stub; v1.6.1 DojoScreen ctor @0x0016bad8
    void*       m_pBSButton1;    // +0xa4 (BSButton* Twitter social share)
    void*       m_pButton4;      // +0xa8 (HUDControl*)
    int         m_ResetValue;    // +0xac
    float       m_TransitionDelay; // +0xb0
    void*       m_pVersionText;  // +0xb4 (BakedStringBox*)

    // Port-only tail (beyond binary 0xb8 boundary, does not shift binary fields):
    // Child AboutScreen when state==3 triggers. nullptr when not shown.
    AboutScreen* m_pAboutScreen;

    // Port specific: binary accesses Game via GOT; port stores a reference here.
    Game& game;

    // Binary stores textures at GOT-relative globals, not per-instance.
    // Port mirrors this with static members so LoadContent/UnLoadContent
    // can be called from GameInitialise/GameDestroy independently of
    // any DojoScreen instance.
    static Mortar::SmartPtr<Mortar::Texture> s_TexDojo;        // +0x0c: dojo.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexSensei;      // +0x10: dojo_sensei.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexShop;        // +0x14: senseis_swag.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexAbout;       // +0x18: about.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexBackIcon;    // back_icon.tex

    // --- Callbacks ---
    void PlayCallback();
    void ShopCallback();
    void AboutCallback();

public:
    // Defunct: more-games/online dashboard upsell -- no-op stub; v1.5.x @ 0x0013769c (TODO: re-verify v1.6.1)
    void MoreGamesCallback();
    // Defunct: iOS Quit-from-Dojo callback -- no-op stub; binary symbol exists
    // with ZERO Bada call-site xrefs (iPhone/iPad-variant leftover .o).
    //
    // NOTE: the Back button click delegate (Play -> state 6) is implemented as
    // DojoScreen::PlayCallback() in DojoScreen.cpp (not a separate QuitCallback).
    void QuitCallback();
    // Defunct: online network-provider selection -- no-op stub; v1.5.x @ 0x00137694 (TODO: re-verify v1.6.1)
    void SwitchCallback();
    // Defunct: online network-provider button -- no-op stub; v1.5.x @ 0x001379b0 (TODO: re-verify v1.6.1)
    void SwitchNetworkButton(MenuButton*, float, ScreenButton&);
    // Defunct: Twitter/Facebook social-share button layout/animation -- no-op stub; v1.5.x @ 0x00137738 (TODO: re-verify v1.6.1)
    void TwitterFacbookButtons(MenuButton*, float, ScreenButton&);

#ifdef __bada__
    friend struct DojoScreenLayoutAssert;
#endif
};

#ifdef __bada__
#include <cstddef>
// Binary sizeof(DojoScreen) == 0xb8 (v1.6.1 DojoScreen ctor @0x0016bad8).
// Port-only tail (m_pAboutScreen, game) pushes sizeof past 0xb8, so we assert
// offsetof on the binary-faithful prefix only (same pattern as MainScreen).
// Friend struct: GCC 4.4 rejects offsetof on private members from namespace scope.
struct DojoScreenLayoutAssert {
static_assert(__builtin_offsetof(DojoScreen, m_pBackButton)      == 0x94, "m_pBackButton offset");
static_assert(__builtin_offsetof(DojoScreen, m_pShopButton)      == 0x98, "m_pShopButton offset");
static_assert(__builtin_offsetof(DojoScreen, m_pAboutButton)     == 0x9c, "m_pAboutButton offset");
static_assert(__builtin_offsetof(DojoScreen, m_pBSButton0)       == 0xa0, "m_pBSButton0 offset");
static_assert(__builtin_offsetof(DojoScreen, m_pBSButton1)       == 0xa4, "m_pBSButton1 offset");
static_assert(__builtin_offsetof(DojoScreen, m_pButton4)         == 0xa8, "m_pButton4 offset");
static_assert(__builtin_offsetof(DojoScreen, m_ResetValue)       == 0xac, "m_ResetValue offset");
static_assert(__builtin_offsetof(DojoScreen, m_TransitionDelay)  == 0xb0, "m_TransitionDelay offset");
static_assert(__builtin_offsetof(DojoScreen, m_pVersionText)     == 0xb4, "m_pVersionText offset");
};
#endif

#endif
