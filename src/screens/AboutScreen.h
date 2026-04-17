#ifndef FN_ABOUT_SCREEN_H
#define FN_ABOUT_SCREEN_H

//
// AboutScreen : HUDControl3d (size ~0xa0)
//
// Binary refs (docs/screens/about.md):
//   Constructor 0x0012ecb8  (takes DojoScreen* parent)
//   Update      0x0012f020
//   Draw        0x0012f394
//
// Credits / about page shown as a child of DojoScreen. Single back
// button + pre-rendered credits texture.
//
// State machine:
//   0 = transition-in: lerp m_TransitionAlpha → 1.0 (step 0.125)
//   1 = idle
//   2 = fade out, pending removal → DojoScreen resumes state 1
//

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
class DojoScreen;
struct Game;

class AboutScreen : public HUDControl3d {
public:
    AboutScreen(Game& g, DojoScreen* parent);
    ~AboutScreen();

    void Init() override;
    void Release() override;
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    int  GetType() override { return 1; }

    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    static void LoadContent() {}   // TODO: 0x12ec14
    static void UnLoadContent() {} // TODO

private:
    Game& game;
    DojoScreen* m_pParent;   // +0x90: parent DojoScreen (back navigation)

    // AboutScreen has its own alpha/state — it does NOT inherit BaseScreen.
    // Binary ctor calls HUDControl3d::HUDControl3d directly.
    int   m_State;             // +0x94
    float m_TransitionAlpha;   // +0x7C (field106_0x7c in binary)

    MenuButton* m_pBackButton;
    MenuButton* m_pCreditsButton;   // optional (binary has this)

    SmartPtr<Mortar::Texture> m_TexAbout;    // about.tex: page background
    SmartPtr<Mortar::Texture> m_TexCredits;  // credits.tex: pre-rendered text

    bool m_bButtonsCreated;

    void CreateButtons();
    void RemoveButtons();

    // Back button callback → begins the fade-out.
    void BackCallback();
};

#endif
