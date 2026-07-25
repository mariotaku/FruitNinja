#ifndef FN_HUD_ZEN_VERSUS_CONTROL_H
#define FN_HUD_ZEN_VERSUS_CONTROL_H

//
// ZenVersusControl : HUDControl3d
//
// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 ZenVersusControl @0x000882c4
//
// In-game online-VERSUS HUD: a top balance slider (whose fill leans toward
// whichever player is ahead) plus each player's live score readout and name.
// iOS 1.6.1 size 0x108, ctor @0x000882c4, LoadContent @0x000880d8,
// DrawOrder @0x00089664 (thin image base 0x1000 -- addresses are file offsets,
// not absolute VAs). Bada v1.6.1 shipped the three slider textures
// (slider.tex / slider_bar.tex / slide_bar_wifi_multi.tex) but the class
// itself has no symbol in the Bada binary -- this is a from-scratch port
// against the iOS 1.6.1 spec, not an asm-verified Bada function.
//
// While active it REPLACES the local ScoreControl's on-screen readout:
// ScoreControl::Draw/PreDraw already self-hide when (m_PlayerIdx==0 &&
// IsMultiplayer()) -- see ScoreControl.cpp -- so no change was needed there;
// this control simply draws its own score/name pair instead.
//
// Contract:
//   * LoadContent() is a static, lazy, load-once texture fetch. Call it
//     before constructing (or the ctor will call it for you -- see .cpp).
//   * Construct with `new ZenVersusControl()` and HUD::AddControl it (see
//     CreateMultiplayerControls() in P2PMessageHandling.cpp) -- normal
//     HUDControl3d lifetime, no special teardown.
//   * P0's score is game_work.currentScore (the port's existing single-slot
//     score, see ScoreControl::GetCurrentScore); P1's score is
//     Mortar::NetworkManager::GetOpponentScore() (already wired by
//     PointsPacket handling in P2PMessageHandling.cpp). Names come from
//     GameWork::GetPlayerName(0)/(1) -- see GameWork.h.
//   * Standalone/test construction: the ctor and Draw() have no other
//     dependency beyond LoadContent() + a valid game_work font slot (for the
//     name/score text) -- game_work.mHud may be null (only used for the
//     alpha term also read by ScoreControl).
//

#include "HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <cstdint>

class ZenVersusControl : public HUDControl3d {
public:
    // Per-player (index 0 = local, 1 = opponent) eased score display.
    float    m_ScoreSmoothed[2];   // lerped toward the live int score each frame
    int      m_ScoreInt[2];        // (int)m_ScoreSmoothed[i]
    uint16_t m_PulseAngle[2];      // sin-table angle; kicked on score increase, decays to 0
    float    m_ScoreScale[2];      // 1.0 + pulse-driven pop scale

    // clamp((P1-P0)/20, -1, 1) -- balance slider fill: <0 leans P0, >0 leans P1.
    float m_SliderBias;

    char m_ScoreStr0[32];  // sprintf("%i", score) for P0 (local)
    char m_ScoreStr1[32];  // sprintf("%i", score) for P1 (opponent)

    // Slider grow-in on session start: m_IntroTimer 0->1 over the intro,
    // m_IntroScale = SinIdx-eased ratio driving the slider's vertical scale.
    float m_IntroScale;
    float m_IntroTimer;

    // Slider vertical bob.
    uint16_t m_WobbleAngle;
    float    m_WobbleOffset;

    uint8_t m_bScoreDirty;    // 1 = re-sprintf m_ScoreStr0/1 this frame
    uint8_t m_bDisconnected;  // 1 = opponent has left (draws disconnected state)

    ZenVersusControl();
    ~ZenVersusControl() override;

    void Update(float dt) override;
    void PreDraw(float* hudScale) override;
    void Draw(float* hudScaleRaw) override;
    void DrawOrder(float* hudScale, int layerMask) override;
    int  GetType() override { return 1; } // HUDControl3d default (no dedicated binary type tag found)

    // iOS 1.6.1 ZenVersusControl::LoadContent @0x000880d8 -- loads the three
    // slider textures once (s_slider / s_sliderBar / s_sliderBarWifi), all
    // confirmed present in the shipped Bada Data dump. Safe to call more than
    // once; only the first call issues the texture loads.
    static void LoadContent();

    // Port-only helpers for standalone/test construction, not RE'd binary API.
    // Sets the underlying score sources AND snaps the eased display state
    // (m_ScoreSmoothed/m_ScoreInt/m_ScoreStr0/1/m_SliderBias) to the exact
    // target immediately, with no pulse pop (m_ScoreScale=1, m_PulseAngle=0)
    // -- a render test wants the exact set scores on screen with zero or one
    // Update() call, not a mid-ease asymptotic value. Normal gameplay still
    // eases via Update(dt); this bypasses that path only for test setup.
    void SetScoresForTest(int p0, int p1);
    void SetDisconnectedForTest(bool disconnected) { m_bDisconnected = disconnected ? 1 : 0; }

private:
    static Mortar::SmartPtr<Mortar::Texture> s_slider;         // "slider.tex" -- balance fill
    static Mortar::SmartPtr<Mortar::Texture> s_sliderBar;      // "slider_bar.tex" -- (unused by DrawOrder's two-texture bar; reserved slot, see .cpp)
    static Mortar::SmartPtr<Mortar::Texture> s_sliderBarWifi;  // "slide_bar_wifi_multi.tex" -- top bar backdrop
    static bool s_hasLoadedContent;
};

#endif // FN_HUD_ZEN_VERSUS_CONTROL_H
