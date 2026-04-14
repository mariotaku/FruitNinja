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

private:
    Game& game;
    DojoScreen* m_pParent;   // parent DojoScreen (back navigation)

    float m_TransitionAlpha; // BaseScreen +0x7c
    int   m_State;           // BaseScreen +0x9c

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
