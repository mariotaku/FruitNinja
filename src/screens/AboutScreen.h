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
//   haikus.tex     -> s_TexHaiku   (background panel, copied to field_0x74 in ctor)
//   credits.tex    -> s_TexCredits (slides up from bottom in Draw block 3)
//   sensei.tex     -> s_TexSensei  (slides in from right in Draw block 4)
//
// Instance fields (beyond HUDControl3d base at +0x7C):
//   +0x74  SmartPtr<Texture>  field_0x74      haiku panel tex (per-instance copy)
//   +0x7C  float              m_TransitionAlpha  0->1 lerp / decay
//   +0x8C  MenuButton*        m_pBackButton   back button (created lazily at alpha>0.999)
//   +0x90  DojoScreen*        m_pParent       parent back-navigation
//   +0x94  MenuButton*        m_pOFNButton    OpenFeint/GameCenter btn (defunct, stub)
//   +0x98  SmartPtr<Texture>  field_0x98      (null in port, OFN overlay tex in binary)
//   +0x9C  int                m_State         0=in, 1=idle, 2=out
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
struct Game;

class AboutScreen : public HUDControl3d {
public:
    // Matches AboutScreen::AboutScreen @ 0x0012ecb8
    AboutScreen(Game& g, DojoScreen* parent);

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

private:
    Game& game;

    // +0x90 in binary
    DojoScreen* m_pParent;

    // +0x94: OpenFeint/GameCenter button — stub (defunct online feature)
    MenuButton* m_pOFNButton;

    // +0x8C: back button (created lazily when alpha crosses 0.9990)
    MenuButton* m_pBackButton;

    // +0x7C in binary (field106_0x7c)
    float m_TransitionAlpha;

    // +0x9C in binary (field126_0x9c)
    int m_State;

    // +0x74: per-instance copy of s_TexHaiku (queried for dimensions)
    SmartPtr<Mortar::Texture> m_TexHaiku;

    // +0x98: additional SmartPtr — null in port (OFN overlay in binary)
    SmartPtr<Mortar::Texture> m_TexOFNOverlay;

    // Static textures (GOT-relative globals in binary, LoadContent manages them)
    static SmartPtr<Mortar::Texture> s_TexHaiku;    // haikus.tex  (DAT_0012eca0 slot)
    static SmartPtr<Mortar::Texture> s_TexCredits;  // credits.tex (DAT_0012eca8 slot)
    static SmartPtr<Mortar::Texture> s_TexSensei;   // sensei.tex  (DAT_0012ecb0 slot)

    // One-time init guard (binary: DAT_0012ed94 + 0xc in BSS)
    static bool s_bContentLoaded;

    // Back button callback -> start fade-out (state 2)
    void BackCallback();

    // Helpers
    void CreateBackButton();
    void RemoveBackButton();
};

#endif // FN_ABOUT_SCREEN_H
