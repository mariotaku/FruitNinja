#ifndef FN_GAME_MODE_SCREEN_H
#define FN_GAME_MODE_SCREEN_H

//
// GameModeScreen : HUDControl3d (BaseScreen subclass, size ~0xD0)
//
// Binary refs (docs/screens/game-mode.md):
//   Constructor         0x0013e524 (bool isFromPause)
//   CreateControls      0x0013e764
//   Update              0x0013f10c (212 lines)
//   QuitCallback        0x0013F5E0 (back/quit button)
//   ClassicModeCallback 0x0013dfb4
//   ZenModeCallback     0x0013dffc
//   ArcadeModeCallback  0x0013e19c
//   SetupLevel          0x0013e21c
//
// Child screen spawned by MainScreen state 0x0e/0x0f (STATE_MODE_SELECT)
// when m_Timer2 crosses 0.25 downward (see MainScreen::Update @ 0x0014bf40).
// Offers four buttons: Back, Classic, Zen, Arcade. Picking a mode fades
// out and pushes MainScreen into STATE_CAMERA_FADE (0x11) which then
// drops into the gameplay loop. Back button sets m_State = 0xE, triggering
// MainScreen STATE_SLIDE_IN (8) after fade.
//
// Port omits:
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
    //   +0xa0  m_BtnBack          (back_icon.tex + bomb fruit, QuitCallback)
    //   +0xa4  m_ButtonDelay      (-1 = inactive, else decrements by dt)
    //   +0xa8  m_Unknown_A8       (set to -1 in state 0 transition)
    //   +0xb4  m_SecondaryAlpha   (starts -2.5, lerped toward 0 / 1)
    //   +0xb8  m_IsFromPause
    //   +0xc4  m_LayerFlagsAlt    (0x80)
    //   +0xc8  m_FrameTimer       (drives DrawConnectTexture animation)
    //   Binary 4-button array: back / classic / zen / arcade (no online MP)

    MenuButton* m_pBackButton;       // +0xa0  m_BtnBack  (back_icon.tex, bomb, QuitCallback)
    MenuButton* m_pClassicButton;    // classic.tex, watermelon, ClassicModeCallback
    MenuButton* m_pZenButton;        // mode_2.tex,  apple_red, ZenModeCallback
    MenuButton* m_pArcadeButton;     // arcade_mode.tex, banana, ArcadeModeCallback

    float m_ButtonDelay;             // +0xa4
    float m_SecondaryAlpha;          // +0xb4 (also drives Draw slide-in)
    float m_FrameTimer;              // +0xc8
    bool  m_bIsFromPause;            // +0xb8
    bool  m_bButtonsCreated;

    // Static textures (binary: module-level globals, loaded in LoadContent)
    static SmartPtr<Mortar::Texture> s_TexModeSensei;   // mode_sensei.tex: panel + logo
    static SmartPtr<Mortar::Texture> s_TexModeSelect;   // mode_select.tex: borders
    static SmartPtr<Mortar::Texture> s_TexClassic;      // classic.tex: Classic button panel
    static SmartPtr<Mortar::Texture> s_TexMode2;        // mode_2.tex: Zen button panel
    static SmartPtr<Mortar::Texture> s_TexArcadeMode;   // arcade_mode.tex: Arcade button panel
    static SmartPtr<Mortar::Texture> s_TexComingSoon;   // coming_soon.tex
    static SmartPtr<Mortar::Texture> s_TexZenSign;      // zen_sign.tex: connect animation
    // Port specific: binary reads back-button texture from Game+0x17c
    // (a global SmartPtr — back_icon.tex, matching DojoScreen's back button
    // which reads the same slot). Until we mirror that Game field, load it
    // here so the back button has the correct back-arrow visual.
    static SmartPtr<Mortar::Texture> s_TexBackIcon;     // back_icon.tex: back button (btn 1)

    void CreateControls();
    void RemoveButtons();

    void DrawConnectTexture(const Vec3& pos);  // 0x0013f754

    // vtable[18] @ 0x0013e21c — prime the first wave once the camera fade
    // crosses -0.9. Calls PrepareForLevelStart().
    void SetupLevel();

    // Button callbacks (bound via std::function).
    void QuitCallback();          // 0x0013F5E0 — back button, m_State = 0xE
    void ClassicModeCallback();   // 0x0013dfb4 — m_State = 3
    void ZenModeCallback();       // 0x0013dffc — m_State = 6
    void ArcadeModeCallback();    // 0x0013e19c — m_State = 5

    // Port-only one-shot latch: binary vtable[18] is idempotent but
    // WaveManager::Reset(false) is destructive — guard against per-frame calls.
    bool m_bSetupLevelFired = false;
};

#endif
