#ifndef FN_GAME_MODE_SCREEN_H
#define FN_GAME_MODE_SCREEN_H

//
// GameModeScreen : HUDControl3d (BaseScreen subclass, size ~0xD0)
//
// Binary refs:
//   Constructor              0x0013e524 (bool isFromPause)
//   Reset                    0x0013df80
//   CreateControls           0x0013e764
//   Update                   0x0013f10c (212 lines)
//   UpdateSpecific           0x00140498
//   IsTransitionInFinished   0x0013df94 (bare BX LR, returns 0)
//   QuitCallback             0x0013F5E0
//   ClassicModeCallback      0x0013dfb4
//   ZenModeCallback          0x0013dffc
//   ArcadeModeCallback       0x0013e19c
//   SetupLevel               0x0013e21c
//   SetIsChallenge           0x0013df84
//   CommingsSoonCallback     0x0013e124
//   DeletedMenuButton        0x0013f6ac
//   CasinoModeCallback       0x0013dfdc
//   VersusModeCallback       0x0013e01c
//   P2PConnectCallback       0x0013dfd4
//   BuyNow                   0x0013e10c
//   SwitchToUpsell           0x0013e084
//   UpsellFinished           0x0013e07c
//   ShrinkedMultiplayerButton 0x0013e02c
//   UpdateOnlineMultiplayerButton 0x0013ecdc
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
    void Reset() override;                           // Binary @ 0x0013df80 — no-op override stub
    void Release() override;
    void Update(float dt) override;
    void UpdateSpecific(float dt);                   // Binary @ 0x00140498 — no-op (Update does all work); vtable slot not in ported base yet
    bool IsTransitionInFinished();                   // Binary @ 0x0013df94 — bare BX LR, returns false; vtable slot not in ported base yet
    void Draw(const Vec3& hudScale, int layerMask) override;
    int  GetType() override { return 1; }

    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    // Binary @ 0x0013df84 — sets m_bChallenge=true + stores id and data ptr
    void SetIsChallenge(int challengeId, void* data);

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
    MenuButton* m_pOnlineMpButton;   // null (defunct online MP slot)

    float m_ButtonDelay;             // +0xa4
    float m_SecondaryAlpha;          // +0xb4 (starts -2.5, lerped toward 1)
    float m_FrameTimer;              // +0xc8
    bool  m_bIsFromPause;            // +0xb8
    bool  m_bButtonsCreated;

    // Challenge-invite fields (Binary @ 0x0013df84 SetIsChallenge)
    uint8_t m_bChallenge;            // +0xb9
    int     m_ChallengeId;           // +0xbc
    void*   m_pChallengeData;        // +0xc0

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

    // Button callbacks (bound via Delegate).
    void QuitCallback();          // 0x0013F5E0 — back button, m_State = 0xE
    void ClassicModeCallback();   // 0x0013dfb4 — m_State = 3
    void ZenModeCallback();       // 0x0013dffc — m_State = 6
    void ArcadeModeCallback();    // 0x0013e19c — m_State = 5

    // Binary @ 0x0013e124 — bumps coming_soon save-stat + resets tutorial
    // (typo "Commings" preserved from binary symbol)
    void CommingsSoonCallback();

    // Binary @ 0x0013f6ac — clears m_p*Button cache on MenuButton destroy
    void DeletedMenuButton(MenuButton* btn);

    // Defunct: online MP (Casino) -- no-op stub; binary @ 0x0013dfdc sets m_State=4
    void CasinoModeCallback();

    // Defunct: online MP (Versus) -- no-op stub; binary @ 0x0013e01c sets m_State=7 + alpha=1.0
    void VersusModeCallback();

    // Defunct: P2P connect -- no-op stub; binary @ 0x0013dfd4 sets m_State=8 (GameCenter connect)
    void P2PConnectCallback();

    // Defunct: upsell store handoff -- no-op stub; binary @ 0x0013e10c calls GotoFruitNinjaPage(1,-1) then m_State=0xd
    void BuyNow();

    // Defunct: upsell glue -- UpsellScreen never instantiated; binary @ 0x0013e084 sets m_State=10
    void SwitchToUpsell(int idx);

    // Defunct: upsell return path -- no-op stub; binary @ 0x0013e07c sets m_State=1
    void UpsellFinished();

    // Defunct: online-MP shrink hook -- no-op stub; binary @ 0x0013e02c snapshots fruit pose + zeroes vel/scale
    void ShrinkedMultiplayerButton();

    // Defunct: online-MP button lifecycle -- no-op stub; binary @ 0x0013ecdc
    void UpdateOnlineMultiplayerButton(float dt);

    // Port-only one-shot latch: binary vtable[18] is idempotent but
    // WaveManager::Reset(false) is destructive — guard against per-frame calls.
    bool m_bSetupLevelFired;
};

#endif
