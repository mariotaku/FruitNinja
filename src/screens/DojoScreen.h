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

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
class AboutScreen;
struct Game;

class DojoScreen : public HUDControl3d {
public:
    DojoScreen(Game& g);
    ~DojoScreen();

    // HUDControl overrides
    void Init() override;
    void Release() override;
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

private:
    Game& game;

    // +0x8C (BaseScreen): lerped 0→1 on entry, ×0.75 on exit
    float m_TransitionAlpha;

    // +0x90 (BaseScreen): state machine
    int m_State;

    // +0x94..+0x9c: sub-button pointers (lazy-created in Update state 0)
    MenuButton* m_pPlayButton;
    MenuButton* m_pShopButton;
    MenuButton* m_pAboutButton;

    // Child AboutScreen when state==3 triggers. NULL when no about
    // is shown. Port keeps a weak ptr so the parent can poll
    // m_bPendingRemoval and react.
    AboutScreen* m_pAboutScreen;

    // Binary stores textures at GOT-relative globals, not per-instance.
    // Port mirrors this with static members so LoadContent/UnLoadContent
    // can be called from GameInitialise/GameDestroy independently of
    // any DojoScreen instance.
    static SmartPtr<Mortar::Texture> s_TexDojo;        // +0x0c: dojo.tex
    static SmartPtr<Mortar::Texture> s_TexSensei;      // +0x10: dojo_sensei.tex
    static SmartPtr<Mortar::Texture> s_TexShop;        // +0x14: senseis_swag.tex
    static SmartPtr<Mortar::Texture> s_TexAbout;       // +0x18: about.tex
    // Port specific: binary's DrawBorders loads blurry_backing from a global.
    static SmartPtr<Mortar::Texture> s_TexBlurryBacking;
    static SmartPtr<Mortar::Texture> s_TexBackIcon;

    // --- Helpers ---
    void CreateButtons();
    void RemoveButtons();

    // --- Callbacks ---
    void PlayCallback();
    void ShopCallback();
    void AboutCallback();
};

#endif
