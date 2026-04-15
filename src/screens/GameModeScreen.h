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

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
struct Game;

class GameModeScreen : public HUDControl3d {
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

private:
    Game& game;

    // Binary layout (offsets from decompile):
    //   +0x8c  m_TransitionAlpha  (inherited BaseScreen)
    //   +0x90  m_State            (inherited BaseScreen)
    //   +0xa0  m_ClassicButton
    //   +0xa4  m_ButtonDelay      (-1 = inactive, else decrements by dt)
    //   +0xa8  m_Unknown_A8       (set to -1 in state 0 transition)
    //   +0xb4  m_SecondaryAlpha   (starts -2.5, lerped toward 0 / 1)
    //   +0xb8  m_IsFromPause
    //   +0xc4  m_LayerFlagsAlt    (0x80)
    //   +0xc8  m_FrameTimer
    //
    float m_TransitionAlpha;
    int   m_State;

    MenuButton* m_pClassicButton;
    MenuButton* m_pZenButton;
    MenuButton* m_pArcadeButton;

    float m_ButtonDelay;
    float m_SecondaryAlpha;
    float m_FrameTimer;
    bool  m_bIsFromPause;
    bool  m_bButtonsCreated;

    void CreateControls();
    void RemoveButtons();

    // Sub-button callbacks (bound via std::function).
    void ClassicModeCallback();
    void ZenModeCallback();
    void ArcadeModeCallback();
};

#endif
