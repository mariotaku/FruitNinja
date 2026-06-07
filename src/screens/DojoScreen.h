#ifndef FN_DOJO_SCREEN_H
#define FN_DOJO_SCREEN_H

//
// DojoScreen : HUDControl3d (BaseScreen subclass, size ~0xa4)
//
// Binary refs (docs/screens/dojo.md + docs/screens/common-patterns.md):
//   Constructor 0x00137b90
//   Update      0x00138414 (247 lines)
//   Draw        0x0013822c
//
// Secondary menu shown after tapping the Dojo button on MainScreen.
// Has three sub-buttons: Play (return to game), Shop (blade/power-up
// shop — stubbed for port), About (credits).
//
// State machine:
//   0 = transition-in: lerp m_TransitionAlpha → 1.0 (step 0.25),
//       create 3 buttons, → state 1
//   1 = idle
//   2 = fade out, → ShopScreen  (port: stub, falls through to 6)
//   3 = fade out, → AboutScreen
//   6 = fade out, pending removal → MainScreen STATE_SLIDE_IN
//
// Port specific:
//   - No sensei 3D animation (binary has an animated 3D model).
//   - Shop button stub — returns to MainScreen instead of opening ShopScreen.
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
    void Draw(const Vec3& hudScale, int layerMask) override;
    int  GetType() override { return 1; }

    // Binary stores textures at GOT-relative static storage.
    static void LoadContent();    // 0x00137a20
    static void UnLoadContent();  // 0x00137c04

    // True when the screen has completed its fade-out and wants to
    // be removed from the HUD. MainScreen polls this to transition
    // back to STATE_SLIDE_IN.
    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    // Matches DojoScreen::ButtonDeleted @ 0x00137684.
    // Remove callback for the shop button (field_0x98). Binary only
    // installs this on the shop button, not play or about.
    void ButtonDeleted(HUDControl* ctrl);

    // Port-specific: remove callback helper for AboutScreen child pointer.
    void AboutScreenRemoved(HUDControl*) { m_pAboutScreen = nullptr; }

private:
    // +0x94..+0x9c: sub-button pointers (lazy-created in Update state 0)
    MenuButton* m_pPlayButton;
    MenuButton* m_pShopButton;
    MenuButton* m_pAboutButton;

    // Child AboutScreen when state==3 triggers. nullptr when no about
    // is shown. Port keeps a weak ptr so the parent can poll
    // m_bPendingRemoval and react.
    AboutScreen* m_pAboutScreen;

    // Port specific: binary accesses Game via GOT; port stores a reference here,
    // declared after all binary-faithful fields so it does not displace them.
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

#ifdef __bada__
    friend struct DojoScreenLayoutAssert;
#endif

public:
    // Defunct: more-games/online dashboard upsell (NetworkManager::LaunchDashboard) -- no-op stub; binary @ 0x0013769c
    void MoreGamesCallback();
    // Defunct: iOS Quit-from-Dojo callback -- no-op stub; binary symbol exists
    // with ZERO Bada call-site xrefs (iPhone/iPad-variant leftover .o).
    //
    // NOTE on 0x001389f4: that address is NOT a distinct QuitCallback. It is the
    // click delegate the binary installs on the Play button (field +0x94) in
    // DojoScreen::Update @ 0x001384a8 (delegate fn-ptr = 0x001389f4). Its logic
    // -- SFXPlay("menu-bomb",1,1); m_State=6; set m_pPlayButton->m_pFruitPiece
    // visible (+0x80=1) and fling it Vec3(rand[0,5)+5, -rand[0,5), 0); then
    // TutorialControl::ResetTutePos(0) -- is already ported faithfully as
    // DojoScreen::PlayCallback() (DojoScreen.cpp). z velocity = DAT_00138acc = 0.0f.
    void QuitCallback();
    // Defunct: online network-provider selection (AskUserToChoosePreferredNetwork) -- no-op stub; binary @ 0x00137694
    void SwitchCallback();
    // Defunct: online network-provider button (NetworkManager::GetPreferredNetworkProvider texture swap) -- no-op stub; binary @ 0x001379b0
    void SwitchNetworkButton(MenuButton*, float, ScreenButton&);
    // Defunct: Twitter/Facebook social-share button layout/animation -- no-op stub; binary @ 0x00137738
    void TwitterFacbookButtons(MenuButton*, float, ScreenButton&);
};

#ifdef __bada__
#include <cstddef>
struct DojoScreenLayoutAssert {
    static_assert(offsetof(DojoScreen, m_pPlayButton)  == 0x94, "m_pPlayButton offset");
    static_assert(offsetof(DojoScreen, m_pShopButton)  == 0x98, "m_pShopButton offset");
    static_assert(offsetof(DojoScreen, m_pAboutButton) == 0x9c, "m_pAboutButton offset");
    static_assert(offsetof(DojoScreen, m_pAboutScreen) == 0xa0, "m_pAboutScreen offset");
};
#endif

#endif
