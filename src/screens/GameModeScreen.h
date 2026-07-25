#ifndef FN_GAME_MODE_SCREEN_H
#define FN_GAME_MODE_SCREEN_H

//
// GameModeScreen : HUDControl3d (BaseScreen subclass, size 0xDC)
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
//   DeletedMenuButton        0x00183814
//   CasinoModeCallback       0x0013dfdc
//   VersusModeCallback       v1.6.1 @0x00181140
//   P2PConnectCallback       v1.6.1 @0x001810dc
//   BuyNow                   0x0013e10c
//   SwitchToUpsell           0x0013e084
//   UpsellFinished           0x0013e07c
//   ShrinkedMultiplayerButton      v1.6.1 @0x00181160
//   UpdateOnlineMultiplayerButton  v1.6.1 @0x0018234c
//   DrawConnectTexture             v1.6.1 @0x001838d8
//
// Child screen spawned by MainScreen state 0x0e/0x0f (STATE_MODE_SELECT)
// when m_Timer2 crosses 0.25 downward (see MainScreen::Update @ 0x0014bf40).
// Offers four buttons: Back, Classic, Zen, Arcade -- plus a 5th "VS" ring
// (m_pOnlineMpButton) that grows in only while an online-MP transport is
// connected (see MP-revival note below). Picking a mode fades out and
// pushes MainScreen into STATE_CAMERA_FADE (0x11) which then drops into the
// gameplay loop. Back button sets m_State = 0xF, triggering MainScreen
// STATE_SLIDE_IN (8) after fade.
//
// Internal sub-state map (v1.6.1):
//   0  = transition-in
//   1  = alternate transition-in (from state-9 network recovery)
//   2  = idle (waiting for input)
//   3  = Classic mode picked
//   4  = Casino/multiplayer mode picked
//   5  = Arcade mode picked
//   6  = Zen mode picked
//   0xe = Challenges (passive — re-analyst resolving ChallengeMenuScreen push)
//   0xf = back-out (was 0xe in v1.5.1)
//
// MP-revival (feat/mp-revival, port-only): the mode-select VS button + online
// ring layout are un-gated from IsP2PSupported() (see P2PMessageHandling.cpp)
// so this screen now shows the fifth "VS" ring and the P2P button layout
// under !__bada__. __bada__ (production/cross-build fidelity target) keeps
// IsP2PSupported() hardcoded false, so it still takes the original stripped
// (offline-only) code paths below.
//
// Port omits (still, even under the revival):
//   - State 1 (alternate-entry from state-9 network recovery) -- unreachable,
//     kept for a faithful state machine (same as v1.5.1).
//

#include "BaseScreen.h"
#include <cstdint>

class MenuButton;
struct Game;

namespace Mortar {
    class BakedStringBox;
}

class GameModeScreen : public BaseScreen {
public:
    GameModeScreen(Game& g, bool isFromPause);
    ~GameModeScreen();

    // HUDControl overrides
    // Binary Init @ 0x00181060 (v1.6.1) -> forwards to Reset @ 0x00181074 (bare BX LR). No-op.
    // Activation is in the ctor; Init() must NOT be called to enable the screen.
    void Init() override;
    void Reset() override;                           // Binary @ 0x0013df80 — no-op override stub
    void Release() override;
    void Update(float dt) override;
#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Advances the per-present (dt-scaled) VISUAL-ONLY parts of state 2 (alpha
    // settle + m_SecondaryAlpha lerp) and the WHOLE state-0xf back-out body
    // (decay + m_SecondaryAlpha mirror + BOTH threshold crossings, kept atomic
    // -- see GameModeScreen.cpp for why). States 0/1 are NOT split: their alpha
    // lerp is the only proxy for IsTransitionInFinished and its threshold check
    // fires CreateControls (a HUD control-list mutation forbidden here), so both
    // stay together in Update() at 60Hz. Update() also keeps m_ButtonDelay/input
    // handling (state 2) and states 3-6 entirely.
    void UpdateRealtime(float dtSeconds) override;
#endif
    void UpdateSpecific(float dt);                   // Binary @ 0x00140498 — no-op (Update does all work); vtable slot not in ported base yet
    bool IsTransitionInFinished();                   // Binary @ 0x0013df94 — bare BX LR, returns false; vtable slot not in ported base yet
    void Draw(float* hudScaleRaw) override;
    int  GetType() override { return 1; }

    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    // Binary @ 0x0013df84 — sets m_bChallenge=true + stores id and data ptr
    // ASM-spec v1.6.1 GameModeScreen::SetIsChallenge @0x00181078: 2nd param is int, not void*.
    void SetIsChallenge(int challengeId, int data);

    static void LoadContent();    // 0x13e330
    static void UnLoadContent();  // 0x13e5a8

    // Binary @ 0x00183814 (v1.6.1) — clears m_p*Button cache on HUDControl destroy.
    // Binary sig: DeletedMenuButton(HUDControl*) -- body casts to MenuButton* since
    // only MenuButtons are ever registered via BtnDeletedFn.
    // Public: called from BtnDeletedFn helper in GameModeScreen.cpp.
    void DeletedMenuButton(HUDControl* ctrl);

public:
    // Binary struct layout (0xDC = 220 bytes total):
    //   BaseScreen base 0x00..0x93 (148 bytes)
    //   +0x94..+0x9F  12-byte gap (BaseScreen tail / alignment; Ghidra-confirmed)
    //   +0xa0  m_BtnBack          (back_icon.tex + bomb fruit, QuitCallback)
    //   +0xa4  m_ButtonDelay      (-1 = inactive, else decrements by dt)
    //   +0xa8  m_TransitionTimer  (set to -1 in state 0 transition)
    //   +0xac  m_pClassicButton   (classic.tex, watermelon)
    //   +0xb0  m_pZenButton       (mode_2.tex, apple_red)
    //   +0xb4  m_SecondaryAlpha   (starts -2.5, lerped toward 1)
    //   +0xb8  m_bIsFromPause     (ctor bool param)
    //   +0xb9  m_bChallenge       (= 0; set by SetIsChallenge)
    //   +0xba..+0xbb  2-byte pad
    //   +0xbc  m_ChallengeId      (= 0)
    //   +0xc0  m_pChallengeData   (int; = 0)
    //   +0xc4  m_LayerFlagsAlt    (0x80; int32)
    //   +0xc8  m_FrameTimer       (drives DrawConnectTexture animation)
    //   +0xcc  m_pArcadeButton    (arcade_mode.tex, banana)
    //   +0xd0  m_pTitleBox  (BakedStringBox* from LocalizedString 0x3be/0x3bf/0x3c0)
    //   +0xd4  m_pDescBox   (BakedStringBox* from LocalizedString 0x3ba)
    //   +0xd8  m_pInfoBox   (BakedStringBox* from LocalizedString 0x39f)

    // +0x94: 12-byte gap between BaseScreen tail and first own member.
    uint8_t _pad_0x94[12];

    MenuButton* m_pBackButton;       // +0xa0: m_BtnBack (back_icon.tex, bomb, QuitCallback)
    float m_ButtonDelay;             // +0xa4: -1 = inactive, else decrements by dt
    float m_TransitionTimer;         // +0xa8: set to -1 in state 0 transition
    MenuButton* m_pClassicButton;    // +0xac: classic.tex, watermelon, ClassicModeCallback
    MenuButton* m_pZenButton;        // +0xb0: mode_2.tex, apple_red, ZenModeCallback
    float m_SecondaryAlpha;          // +0xb4: starts -2.5, lerped toward 1
    bool  m_bIsFromPause;            // +0xb8: ctor param
    uint8_t m_bChallenge;            // +0xb9: set by SetIsChallenge
    uint8_t _pad_0xba[2];            // +0xba: 2-byte alignment pad to bring m_ChallengeId to 0xbc
    int     m_ChallengeId;           // +0xbc: challenge invite id
    int     m_pChallengeData;        // +0xc0: challenge data (int, not ptr -- SetIsChallenge's 2nd param is int)
    int     m_LayerFlagsAlt;         // +0xc4: = 0x80
    float   m_FrameTimer;            // +0xc8: drives DrawConnectTexture animation
    MenuButton* m_pArcadeButton;     // +0xcc: arcade_mode.tex, banana, ArcadeModeCallback
    Mortar::BakedStringBox* m_pTitleBox;  // +0xd0: mode text (LocalizedString 0x3be/0x3bf/0x3c0 three-liner)
    Mortar::BakedStringBox* m_pDescBox;   // +0xd4: desc text (LocalizedString 0x3ba = "MODE SELECT")
    Mortar::BakedStringBox* m_pInfoBox;   // +0xd8: info text (LocalizedString 0x39f = "MULTIPLAYER")

    // Port-specific trailing fields (not in the 220-byte binary struct).
    // Excluded on the __bada__ production build so sizeof stays at 0xdc.
#if !defined(__bada__)
    // Binary accesses Game via GOT; port stores a pointer here.
    Game* m_pGame;
    // One-shot latch for SetupLevel call (port-only idempotency guard).
    bool m_bSetupLevelFired;
#if defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 1 -- true while BlockLoader::PreloadBlockStep() is still
    // draining the INGAME work-queue. Holds the mode-select panel at full
    // opacity (skips the shrink+drop decay) and keeps HUD::SetInputModal(this)
    // + the picked button's loading-symbol armed until the queue drains.
    // Armed in the picked mode's *ModeCallback (ClassicModeCallback etc, see
    // GameModeScreen.cpp's ArmModeLoading), NOT in Update state 3-6 -- arming
    // a frame later there raced the button's own shrink-out reaping itself
    // before the arm could resolve a valid button pointer (task #66 flaky
    // spinner fix). Update state 3-6 only drains + disarms.
    bool m_bLoading;
#endif
    // MP-revival: the 5th "VS" ring MenuButton, grown/shrunk by
    // UpdateOnlineMultiplayerButton. Port-only: the real 220-byte (0xdc)
    // binary struct has no room for this slot, so it lives here rather than
    // in the offsetof-asserted layout above; __bada__ excludes it entirely.
    // DeletedMenuButton nulls it on HUD reap.
    MenuButton* m_pOnlineMpButton;
#endif // !defined(__bada__)

    void CreateControls();
    void RemoveButtons();

    void DrawConnectTexture(_Vector3<float> pos);  // v1.6.1 @0x001838d8

    // vtable[18] @ 0x0013e21c — prime the first wave once the camera fade
    // crosses -0.9. Calls PrepareForLevelStart().
    void SetupLevel();

#if defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 1 -- port-only helper, maps game_work.gameMode (set by
    // the picked mode's *ModeCallback just before m_State enters 3-6) to that
    // mode's MenuButton. Used only by the drain's disarm side (Update state
    // 3-6, SetLoadingSymbol(false)) -- the arm side reads the button field
    // directly inside the firing *ModeCallback instead (see .cpp), since by
    // the time Update runs next frame the button may have already reaped.
    // gameMode: 0=Classic, 2=Arcade, 3=Zen. Returns nullptr if the button
    // already self-removed (shrink-out race) -- caller null-checks.
    MenuButton* PickedModeButton();
#endif

    // Button callbacks (bound via Delegate).
    void QuitCallback();          // 0x0013F5E0 — back button, m_State = 0xF
    void ClassicModeCallback();   // 0x0013dfb4 — m_State = 3
    void ZenModeCallback();       // 0x0013dffc — m_State = 6
    void ArcadeModeCallback();    // 0x0013e19c — m_State = 5

    // Binary @ 0x0013e124 — bumps coming_soon save-stat + resets tutorial
    // (typo "Commings" preserved from binary symbol)
    void CommingsSoonCallback();

    // Defunct: online MP (Casino) -- no-op stub; v1.6.1 binary @ 0x001810e8 sets m_State=4
    void CasinoModeCallback();

    // MP-revival: real click handler -- the "VS" ring's m_ClickCallback (see
    // UpdateOnlineMultiplayerButton). v1.6.1 GameModeScreen::VersusModeCallback
    // @0x00181140 sets m_State=7 + alpha=1.0 (matchmaker entry).
    void VersusModeCallback();

    // Defunct: still unreachable -- no button/native-matchmaker completion calls
    // this in the port (LaunchP2PMatchMaker has no real matchmaker UI to drive
    // it); kept for a faithful state machine. v1.6.1 GameModeScreen::P2PConnectCallback
    // @0x001810dc sets m_State=8 (GameCenter connect).
    void P2PConnectCallback();

    // Defunct: upsell store handoff -- no-op stub; v1.6.1 binary @ 0x00181290 calls GotoFruitNinjaPage(1,-1) then m_State=0xd
    void BuyNow();

    // Defunct: upsell glue -- UpsellScreen never instantiated; v1.6.1 binary @ 0x001811c8 sets m_State=10
    void SwitchToUpsell(int idx);

    // Defunct: upsell return path -- no-op stub; v1.6.1 binary @ 0x001811bc sets m_State=1
    void UpsellFinished();

    // MP-revival: real body under !__bada__ (port-only m_pOnlineMpButton slot,
    // see below) -- snapshots the sliced VS fruit's pose + zeroes vel/scale so
    // it settles instead of drifting. v1.6.1 GameModeScreen::ShrinkedMultiplayerButton
    // @0x00181160. __bada__ has no m_pOnlineMpButton slot (sizeof==0xdc
    // constraint) so stays a no-op there.
    void ShrinkedMultiplayerButton();

    // MP-revival: real body under !__bada__ -- grows/shrinks the 5th "VS"
    // MenuButton based on live transport connectivity (IsP2POnline() /
    // IsP2PConnecting()). v1.6.1 GameModeScreen::UpdateOnlineMultiplayerButton
    // @0x0018234c. __bada__ has no m_pOnlineMpButton slot (sizeof==0xdc
    // constraint) so stays a no-op there, matching the retail (always
    // !IsP2PSupported()) behaviour.
    void UpdateOnlineMultiplayerButton(float dt);

};

#if defined(__bada__)
#include <cstddef>
static_assert(offsetof(GameModeScreen, _pad_0x94)            == 0x94, "_pad_0x94 offset");
static_assert(offsetof(GameModeScreen, m_pBackButton)        == 0xa0, "m_pBackButton offset");
static_assert(offsetof(GameModeScreen, m_ButtonDelay)        == 0xa4, "m_ButtonDelay offset");
static_assert(offsetof(GameModeScreen, m_TransitionTimer)    == 0xa8, "m_TransitionTimer offset");
static_assert(offsetof(GameModeScreen, m_pClassicButton)     == 0xac, "m_pClassicButton offset");
static_assert(offsetof(GameModeScreen, m_pZenButton)         == 0xb0, "m_pZenButton offset");
static_assert(offsetof(GameModeScreen, m_SecondaryAlpha)     == 0xb4, "m_SecondaryAlpha offset");
static_assert(offsetof(GameModeScreen, m_bIsFromPause)       == 0xb8, "m_bIsFromPause offset");
static_assert(offsetof(GameModeScreen, m_bChallenge)         == 0xb9, "m_bChallenge offset");
static_assert(offsetof(GameModeScreen, m_ChallengeId)        == 0xbc, "m_ChallengeId offset");
static_assert(offsetof(GameModeScreen, m_pChallengeData)     == 0xc0, "m_pChallengeData offset");
static_assert(offsetof(GameModeScreen, m_LayerFlagsAlt)      == 0xc4, "m_LayerFlagsAlt offset");
static_assert(offsetof(GameModeScreen, m_FrameTimer)         == 0xc8, "m_FrameTimer offset");
static_assert(offsetof(GameModeScreen, m_pArcadeButton)      == 0xcc, "m_pArcadeButton offset");
static_assert(offsetof(GameModeScreen, m_pTitleBox)          == 0xd0, "m_pTitleBox offset");
static_assert(offsetof(GameModeScreen, m_pDescBox)           == 0xd4, "m_pDescBox offset");
static_assert(offsetof(GameModeScreen, m_pInfoBox)           == 0xd8, "m_pInfoBox offset");
static_assert(sizeof(GameModeScreen)                         == 0xdc, "GameModeScreen size must match binary");
#endif

#endif
