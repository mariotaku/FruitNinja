//
// ZenVersusControl : HUDControl3d
//
// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 ZenVersusControl @0x000882c4
//

#include "ZenVersusControl.h"
#include "hud/HUDLayer.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "math/MathUtil.h"
#include "game/GameWork.h"
#include "engine/network/NetworkManager.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

Mortar::SmartPtr<Mortar::Texture> ZenVersusControl::s_slider;
Mortar::SmartPtr<Mortar::Texture> ZenVersusControl::s_sliderBar;
Mortar::SmartPtr<Mortar::Texture> ZenVersusControl::s_sliderBarWifi;
bool ZenVersusControl::s_hasLoadedContent = false;

// iOS 1.6.1 ZenVersusControl::LoadContent @0x000880d8 -- three
// LoadLocalisedTexture calls, gated by a static "already loaded" bool so
// repeat construction doesn't reload. All three names confirmed present in
// the Bada v1.6.1 Data dump even though the class itself was stripped there.
// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 ZenVersusControl::LoadContent @0x000880d8
void ZenVersusControl::LoadContent() {
    if (s_hasLoadedContent) return;
    s_slider        = Mortar::TextureManager::LoadLocalisedTexture("slider.tex");
    s_sliderBar      = Mortar::TextureManager::LoadLocalisedTexture("slider_bar.tex");
    s_sliderBarWifi  = Mortar::TextureManager::LoadLocalisedTexture("slide_bar_wifi_multi.tex");
    s_hasLoadedContent = true;
}

// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 ZenVersusControl::ZenVersusControl @0x000882c4
// iOS ctor: base HUDControl3d ctor (pos = (0,0,0)); lazy LoadContent() via the
// static guard above; m_LayerFlags byte [0xd] = 0x86 (HUD_LAYER_POST_ACTOR |
// HUD_LAYER_MENU_BG | HUD_LAYER_P2_SCORE -- draws across the post-actor,
// menu-bg, and P2-score passes; see HUDLayer.h for bit meanings).
ZenVersusControl::ZenVersusControl()
    : HUDControl3d()
    , m_SliderBias(0.0f)
    , m_IntroScale(0.0f)
    , m_IntroTimer(0.0f)
    , m_WobbleAngle(0)
    , m_WobbleOffset(0.0f)
    , m_bScoreDirty(1)
    , m_bDisconnected(0)
{
    for (int i = 0; i < 2; ++i) {
        m_ScoreSmoothed[i] = 0.0f;
        m_ScoreInt[i]      = 0;
        m_PulseAngle[i]    = 0;
        m_ScoreScale[i]    = 1.0f;
    }
    m_ScoreStr0[0] = '\0';
    m_ScoreStr1[0] = '\0';

    pos.x = pos.y = pos.z = 0.0f;
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_MENU_BG | Mortar::HUD_LAYER_P2_SCORE;

    LoadContent();
}

ZenVersusControl::~ZenVersusControl() {
}

// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 ZenVersusControl::Update @0x000882c4
// Reads P0 (local, game_work.currentScore) / P1 (opponent,
// NetworkManager::GetOpponentScore()) scores, eases the display toward each,
// derives the balance slider bias, kicks a pulse on increase, and advances
// the intro/wobble animation timers.
void ZenVersusControl::Update(float dt) {
    int scores[2];
    scores[0] = game_work.currentScore;
    scores[1] = Mortar::NetworkManager::GetInstance()->GetOpponentScore();

    for (int i = 0; i < 2; ++i) {
        int prevInt = m_ScoreInt[i];

        // Simple ease toward the live score -- iOS's exact catch-up-rate
        // constant was not recovered from the stripped Bada binary; this
        // uses the same lerp-toward-target shape ScoreControl uses for its
        // own single-player smoothing (see ScoreControl::Update Stage 2).
        float target = (float)scores[i];
        m_ScoreSmoothed[i] += (target - m_ScoreSmoothed[i]) * std::min(1.0f, dt * 6.0f);
        if (std::fabs(target - m_ScoreSmoothed[i]) < 0.5f) {
            m_ScoreSmoothed[i] = target;
        }
        m_ScoreInt[i] = (int)m_ScoreSmoothed[i];

        if (m_ScoreInt[i] > prevInt) {
            m_PulseAngle[i] = 0x8000;
            m_bScoreDirty = 1;
        }

        // Pulse decay -- same shape as ScoreControl's m_PulseAngle decay.
        int decayed = (int)m_PulseAngle[i] + (int)(-327680.0f * dt);
        if (decayed < 0) decayed = 0;
        m_PulseAngle[i] = (uint16_t)decayed;
        m_ScoreScale[i] = 1.0f + SinIdx(m_PulseAngle[i]) * 0.3f;
    }

    m_SliderBias = ((float)scores[1] - (float)scores[0]) / 20.0f;
    if (m_SliderBias < -1.0f) m_SliderBias = -1.0f;
    if (m_SliderBias >  1.0f) m_SliderBias =  1.0f;

    if (m_bScoreDirty) {
        m_bScoreDirty = 0;
        snprintf(m_ScoreStr0, sizeof(m_ScoreStr0), "%i", m_ScoreInt[0]);
        snprintf(m_ScoreStr1, sizeof(m_ScoreStr1), "%i", m_ScoreInt[1]);
    }

    // Intro grow-in: 0 -> 1 over one second, SinIdx-eased.
    if (m_IntroTimer < 1.0f) {
        m_IntroTimer += dt;
        if (m_IntroTimer > 1.0f) m_IntroTimer = 1.0f;
    }
    m_IntroScale = SinIdx((uint16_t)(m_IntroTimer * 0x4000));

    // Slider bob.
    m_WobbleAngle = (uint16_t)(m_WobbleAngle + (uint16_t)(dt * 4000.0f));
    m_WobbleOffset = SinIdx(m_WobbleAngle) * 3.0f;
}

// vtable slot 6 -- iOS PreDraw has no observable body distinct from Update's
// bookkeeping (all per-frame state above already lives in Update); kept as a
// no-op override so the port's DrawOrder dispatch (PreDrawOrder->PreDraw,
// then DrawOrder->Draw) matches the HUDControl3d call shape.
void ZenVersusControl::PreDraw(float* /*hudScale*/) {
}

// Base-class Draw is unused by this control -- ZenVersusControl draws
// multiple independently-positioned quads (bar backdrop, fill, per-player
// text) rather than the single HUDControl3d::Draw quad, so all rendering is
// done in DrawOrder below (mirrors ScoreMultiplyerBoard/MissControl's
// pattern of overriding DrawOrder directly for multi-element HUD widgets).
void ZenVersusControl::Draw(float* /*hudScaleRaw*/) {
}

// DIFFERS: Bada v1.6.1 stripped, revived from iOS 1.6.1 ZenVersusControl::DrawOrder @0x00089664
// Layer passes (see HUDLayer.h HUD::Draw dispatch):
//   BACKGROUND slider (HUD_LAYER_MENU_BG, 0x40): wide backdrop bar across the
//     top of the screen (s_sliderBarWifi), then the balance-fill bar
//     (s_slider) tinted red/blue by m_SliderBias, scaled by the intro/wobble.
//   SCORES + NAMES (HUD_LAYER_P2_SCORE, 0x100): own/opponent score strings
//     via the game font, plus the two player-name labels (P0 blue, P1 red).
void ZenVersusControl::DrawOrder(float* hudScale, int layerMask) {
    if (m_bDisconnected) return;

    MatrixManager& mm = MatrixManager::GetInstance();

    if (layerMask & Mortar::HUD_LAYER_MENU_BG) {
        float barScale = m_IntroScale;
        float barY = 158.0f + m_WobbleOffset;

        // Backdrop bar -- full-width strip at the top of the screen.
        if (s_sliderBarWifi.IsValid()) {
            s_sliderBarWifi->Set();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(320.0f, 20.0f * barScale, 1.0f);
            mat.GlobalTranslate44(_Vector3<float>(0.0f, barY, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
            s_sliderBarWifi->UnSet();
        }

        // Fill bar -- tinted toward whichever player leads, centred by
        // m_SliderBias (< 0 leans P0/blue, > 0 leans P1/red).
        if (s_slider.IsValid()) {
            Colour p0Colour(13, 49, 228, 255);   // blue -- matches P0 name colour below
            Colour p1Colour(126, 0, 255, 255);   // red/violet -- matches P1 name colour below
            float t = (m_SliderBias + 1.0f) * 0.5f; // 0..1
            Colour fillColour(
                (uint8_t)(p0Colour.r + (p1Colour.r - p0Colour.r) * t),
                (uint8_t)(p0Colour.g + (p1Colour.g - p0Colour.g) * t),
                (uint8_t)(p0Colour.b + (p1Colour.b - p0Colour.b) * t),
                255);

            s_slider->Set();
            mm.GetWorldStack().Reset();
            float fillWidth = 320.0f * (0.5f + m_SliderBias * 0.5f);
            Matrix44 mat = Matrix44::MakeScale(fillWidth, 18.0f * barScale, 1.0f);
            mat.GlobalTranslate44(_Vector3<float>(m_SliderBias * -80.0f, barY, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(fillColour, NULL);
            s_slider->UnSet();
        }
    }

    if (layerMask & Mortar::HUD_LAYER_P2_SCORE) {
        Colour white(255, 255, 255, 255);
        Colour p0NameColour(13, 49, 228, 255);
        Colour p1NameColour(126, 0, 255, 255);

        if (game_work.pFontNumbers.IsValid()) {
            game_work.pFontNumbers->DrawString(48.0f * m_ScoreScale[0], 1.0f, 0.0f,
                m_ScoreStr0, _Vector3<float>(-100.0f, 138.0f, 0.0f), white, Mortar::FONT_ALIGN_CENTER);
            game_work.pFontNumbers->DrawString(48.0f * m_ScoreScale[1], 1.0f, 0.0f,
                m_ScoreStr1, _Vector3<float>(100.0f, 138.0f, 0.0f), white, Mortar::FONT_ALIGN_CENTER);
        }

        if (game_work.pFontMain.IsValid()) {
            game_work.pFontMain->DrawString(24.0f, 1.0f, 0.0f,
                game_work.GetPlayerName(0), _Vector3<float>(-235.0f, 158.0f, 0.0f), p0NameColour, Mortar::FONT_ALIGN_CENTER);
            game_work.pFontMain->DrawString(24.0f, 1.0f, 0.0f,
                game_work.GetPlayerName(1), _Vector3<float>(235.0f, 158.0f, 0.0f), p1NameColour, Mortar::FONT_ALIGN_CENTER);
        }
    }

    (void)hudScale;
}

// Port-only test helper -- no binary counterpart. Sets the underlying score
// sources (game_work.currentScore / NetworkManager's opponent-score slot)
// AND snaps the eased display state (m_ScoreSmoothed/m_ScoreInt/strings) to
// the exact target immediately -- a render test wants the exact set scores
// on screen, not a mid-ease asymptotic value. The normal gameplay Update(dt)
// ease path is untouched; this only bypasses it for test setup.
void ZenVersusControl::SetScoresForTest(int p0, int p1) {
    game_work.currentScore = p0;
    Mortar::NetworkManager::GetInstance()->SetOpponentScore(p1);

    int scores[2] = { p0, p1 };
    for (int i = 0; i < 2; ++i) {
        m_ScoreSmoothed[i] = (float)scores[i];
        m_ScoreInt[i]      = scores[i];
        m_PulseAngle[i]    = 0;
        m_ScoreScale[i]    = 1.0f;
    }

    m_SliderBias = ((float)p1 - (float)p0) / 20.0f;
    if (m_SliderBias < -1.0f) m_SliderBias = -1.0f;
    if (m_SliderBias >  1.0f) m_SliderBias =  1.0f;

    m_bScoreDirty = 1;
    snprintf(m_ScoreStr0, sizeof(m_ScoreStr0), "%i", m_ScoreInt[0]);
    snprintf(m_ScoreStr1, sizeof(m_ScoreStr1), "%i", m_ScoreInt[1]);
    m_bScoreDirty = 0;
}
