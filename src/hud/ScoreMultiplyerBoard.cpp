#include "ScoreMultiplyerBoard.h"
#include "game/PowerUp.h"
#include "game/PowerUpManager.h"
#include "game/ScoreDelegate.h"
#include "game/GameOver.h"
#include "game/GameWork.h"
#include "math/MathUtil.h"
#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "render/Font.h"
#include <cstdio>

// v1.6.1 ScoreMultiplyerBoard::ScoreMultiplyerBoard @0x001adf70
// No texture load here -- ScreenEffect::Activate stamps texture/pos/layer/colour
// onto the shared HUDControl3d fields after construction.
ScoreMultiplyerBoard::ScoreMultiplyerBoard()
    : HUDControl3d()
    , m_BasePosition(Vec3::Zero())
    , m_pOwner(0)
    , m_PendingCount(0)
    , m_ScoreValue(-1)
    , m_AnimTime(0.0f)
    , m_Scale(1.0f)
{
}

// v1.6.1 ScoreMultiplyerBoard::Reset @0x001adee4
void ScoreMultiplyerBoard::Reset() {
    m_PendingCount = 0;
    m_ScoreValue = 0;
    m_bPendingRemoval = 1;
}

// ASM-spec v1.6.1 ScoreMultiplyerBoard::Update @0x001ae000
void ScoreMultiplyerBoard::Update(float dt) {
    // Owner mode: PowerUp still active -- mirror the live deferred-points total
    // (the green counter climbs while fruit are sliced under the x2 window).
    if (m_pOwner) {
        m_PendingCount = m_pOwner->m_DeferredPoints;
        return;
    }

    // Freeze gate: paused / not in active gameplay.
    if (game_work.bM_Mode != 0) return;

    float tOld = m_AnimTime;
    m_AnimTime += dt;
    float t = m_AnimTime;

    // Entrance rise, [0, 0.3]: fade 1->0, rise = 1-fade^2 (ease-out).
    float fade = (t <= 0.0f) ? 1.0f : (t < 0.3f ? (1.0f - t / 0.3f) : 0.0f);
    float rise = 1.0f - fade * fade;

    if (m_ScoreValue == 0) {
        // No payout banked (x2 expired without slicing anything) -- retreat
        // and remove; never reaches the payout/slide branch below.
        pos = m_BasePosition - Vec3(0.0f, -70.0f, 0.0f) * rise;
        if (rise > 0.99f) m_bPendingRemoval = 1;
        return;
    }

    // Payout branch.
    pos = m_BasePosition + Vec3(0.0f, -70.0f, 0.0f) * rise;

    // Exit slide, [1.05, 1.3]: eased quadratic slide-out to the left.
    float slide = Clamp((t - 1.05f) * 4.0f, 0.0f, 1.0f);
    slide *= slide;
    pos += Vec3(-150.0f, 0.0f, 0.0f) * slide;

    // Green-counter shrink, [0.3, 0.5]: f goes 1->0, m_Scale follows SinIdx(f*16380)
    // (~1.0 -> 0.0, quarter-turn ease).
    if (t >= 0.3f && t < 0.5f) {
        float f = (t - 0.3f) / 0.2f;
        f = (f <= 0.0f) ? 1.0f : (f < 1.0f ? (1.0f - f) : 0.0f);
        m_Scale = Math::SinIdx((uint16_t)(f * 16380.0f));
    }

    // Bonus-star particle burst, fired once as t crosses 0.5.
    if (tOld < 0.5f && t >= 0.5f) {
        PSPParticleEmitter* emitter = PSPParticleManager::GetInstance().AddEmitter(
            StringHash("bonus_star_impact"), 0, false);
        if (emitter) emitter->m_Pos = pos + Vec3(0.0f, 10.0f, 0.0f);
    }

    // Blue number-pop, [0.5, 0.7]: p goes 0->1, m_Scale = SinIdx(p*20566)/SinIdx(0x5056)*1.35
    // (normalised bounce peaking at 1.35x).
    if (t >= 0.5f && t < 0.7f) {
        float p = Clamp((t - 0.5f) / 0.2f, 0.0f, 1.0f);
        m_Scale = Math::SinIdx((uint16_t)(p * 20566.0f)) / Math::SinIdx((uint16_t)0x5056) * 1.35f;
    }

    // Bank the payout once past 0.8s. Installs an identity score delegate
    // (AddScoreNomals) so AddToCurrentScore doesn't re-apply the ARCADE gain
    // multiplier a second time -- m_ScoreValue is already the doubled total.
    if (t > 0.8f && m_PendingCount > 0) {
        g_ScoreDelegate = &AddScoreNomals;
        AddToCurrentScore(m_ScoreValue, 0, false, false);
        PowerUpManager::GetInstance()->SetAppropriateScoreCallback();
        m_PendingCount = 0;
    }

    if (t > 1.35f) m_bPendingRemoval = 1;
}

// ASM-spec v1.6.1 ScoreMultiplyerBoard::Draw @0x001ae434
void ScoreMultiplyerBoard::Draw(float* /*hudScaleRaw*/) {
    m_DrawColour = Colour(255, 255, 255, 255);
    Vec3 unitScale(1.0f, 1.0f, 1.0f);
    HUDControl3d::Draw(&unitScale.x);

    int value;
    Mortar::SmartPtr<Mortar::Font> font;
    if (m_AnimTime < 0.5f) {
        m_DrawColour = Colour(20, 150, 20);
        value = m_PendingCount;
        font = game_work.pFontNumbers;
    } else {
        m_DrawColour = Colour(255, 255, 255);
        value = m_ScoreValue;
        font = game_work.pFontBlue2;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%i", value); // binary: OS_SPrintf(buf, 32, "%i", value)

    if (font.IsValid()) {
        font->DrawString(m_Scale * 35.0f, 1.0f, 0.0f, buf,
                          pos + Vec3(0.0f, 10.0f, 0.0f), m_DrawColour, 0xF);
    }
}

// ASM-spec v1.6.1 ScoreMultiplyerBoard::Save @0x001ae5dc
// Guards against the board being torn down (e.g. app suspend / game save)
// mid-payout with points still pending -- banks them immediately.
void ScoreMultiplyerBoard::Save() {
    if (m_pOwner == 0 && m_PendingCount > 0) {
        g_ScoreDelegate = &AddScoreNomals;
        AddToCurrentScore(m_ScoreValue, 0, false, false);
        PowerUpManager::GetInstance()->SetAppropriateScoreCallback();
        m_PendingCount = 0;
    }
}
