#ifndef FN_GAME_MODE_SCREEN_H
#define FN_GAME_MODE_SCREEN_H

//
// GameModeScreen : HUDControl3d (BaseScreen subclass, size ~0xD0)
//
// Binary refs (docs/screens/game-mode.md):
//   Constructor      0x0013e524 (bool isFromPause)
//   CreateControls   0x0013e764
//   Update           0x0013f10c (212 lines)
//   ClassicCallback  0x0013dfb4
//   ZenCallback      0x0013dffc
//   ArcadeCallback   0x0013e19c
//   SetupLevel       0x0013e21c
//
// Child screen spawned by MainScreen state 0x0e/0x0f (STATE_MODE_SELECT)
// when m_Timer2 crosses 0.25 downward (see MainScreen::Update @ 0x0014bf40).
// Offers three mode buttons (Classic/Zen/Arcade). Picking a mode fades
// out and pushes MainScreen into STATE_CAMERA_FADE (0x11) which then
// drops into the gameplay loop. Back button (port-specific — binary
// uses case 0xe but has no explicit back UI) sets MainScreen to
// STATE_SLIDE_IN (8).
//
// Port omits:
//   - Multiplayer / matchmaker button (network gone)
//   - Online-vs-offline position swap (always offline layout)
//   - States 1, 7, 8, 9 (alternate-entry + matchmaker recovery)
//

#include "BaseScreen.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
struct Game;

class GameModeScreen : public BaseScreen {
public:
    GameModeScreen(Game& g, bool isFromPause);
    ~GameModeScreen();

    // HUDControl overrides
    void Init() override;
    void Release() override;
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    int  GetType() override { return 1; }

    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    static void LoadContent();    // 0x13e330
    static void UnLoadContent();  // 0x13e5a8

private:
    Game& game;

    // Binary struct layout (0xD0 = 208 bytes total):
    //   +0x8c  m_TransitionAlpha  (inherited BaseScreen)
    //   +0x90  m_State            (inherited BaseScreen)
    //   +0xa0  m_ClassicButton
    //   +0xa4  m_ButtonDelay      (-1 = inactive, else decrements by dt)
    //   +0xa8  m_Unknown_A8       (set to -1 in state 0 transition)
    //   +0xb4  m_SecondaryAlpha   (starts -2.5, lerped toward 0 / 1)
    //   +0xb8  m_IsFromPause
    //   +0xc4  m_LayerFlagsAlt    (0x80)
    //   +0xc8  m_FrameTimer       (drives DrawConnectTexture animation)
    //   +0xcc  m_OnlineMPButton   (skipped for port)

    MenuButton* m_pClassicButton;    // +0xa0
    MenuButton* m_pZenButton;
    MenuButton* m_pArcadeButton;
    MenuButton* m_pMultiplayerButton;  // +0xcc: online MP matchmaker
                                       // (button exists but callback is stubbed)

    float m_ButtonDelay;             // +0xa4
    float m_SecondaryAlpha;          // +0xb4 (also drives Draw slide-in)
    float m_FrameTimer;              // +0xc8
    bool  m_bIsFromPause;            // +0xb8
    bool  m_bButtonsCreated;

    // Static textures (binary: module-level globals, loaded in LoadContent)
    static SmartPtr<Mortar::Texture> s_TexModeSensei;   // mode_sensei.tex: panel + logo
    static SmartPtr<Mortar::Texture> s_TexModeSelect;   // mode_select.tex: borders
    static SmartPtr<Mortar::Texture> s_TexClassic;      // classic.tex: Zen button panel
    static SmartPtr<Mortar::Texture> s_TexMode2;        // mode_2.tex: Arcade1 button panel
    static SmartPtr<Mortar::Texture> s_TexArcadeMode;   // arcade_mode.tex: Arcade2 panel
    static SmartPtr<Mortar::Texture> s_TexComingSoon;   // coming_soon.tex
    static SmartPtr<Mortar::Texture> s_TexZenSign;      // zen_sign.tex: connect animation

    void CreateControls();
    void RemoveButtons();

    // Sub-button callbacks — defunct / skipped but stubbed to keep
    // binary layout/flow intact. See method bodies for details.
    void MatchmakerCallback();   // defunct: opens online MP matchmaker (state 7)
    void DrawConnectTexture(const Vec3& pos);  // 0x0013f754

    // Sub-button callbacks (bound via std::function).
    void ClassicModeCallback();
    void ZenModeCallback();
    void ArcadeModeCallback();
};

#endif
