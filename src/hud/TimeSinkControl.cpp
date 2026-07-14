#include "TimeSinkControl.h"
#include "TimeControl.h"
#include "game/GameWork.h"
#include "game/PowerUp.h"
#include "game/PowerUpManager.h"
#include "game/TimeSinkModifier.h"
#include "engine/util/StringHash.h"
#include "engine/util/Transition.h"
#include "engine/math/MathUtil.h"
#include "engine/render/Font.h"
#include <cstdio>
#include <cmath>

// v1.6.1 TimeSinkControl::TimeSinkControl @0x001c19dc
// No texture load here -- ScreenEffect::Activate stamps texture/pos/layer/colour
// onto the shared HUDControl3d fields after construction (same pattern as
// ScoreMultiplyerBoard).
TimeSinkControl::TimeSinkControl()
    : HUDControl3d()
    , m_DisplayScore(0.0f)
    , m_TargetScore(0.0f)
    , m_unused84(0.0f)
    , m_TimeElapsed(0.0f)
    , m_AnimScale(50.0f)
    , m_JustActivated(false)
    , m_QuantumFlag(0)
    , m_pPowerUp(0)
{
    // ctor @0x001c19dc: m_HudScale = (0, 0.25, 1.0) -- matches the RELEASE
    // phase's fly-in start pose (see Update()'s hudScaleStart).
    m_HudScale = _Vector3<float>(0.0f, 0.25f, 1.0f);
}

// ASM-spec v1.6.1 TimeSinkControl::Update @0x001c1b98:
//   ACTIVE (m_pPowerUp != 0): ratchet m_DrawColour.a toward its running max
//   (m_QuantumFlag -- a uint8 byte, ASM @0x001c1bc0/0x001c1c78); scan
//   m_pPowerUp's modifier list for the TimeSinkModifier (GetType()==4) and
//   mirror its m_Accumulator into m_TargetScore; latch m_JustActivated once
//   PowerUpManager has an active "score_mult" power (StringHash("score_mult")).
//   RELEASE (m_pPowerUp == 0, cleared by ScreenEffect::Deactivate): if
//   m_JustActivated was set, consume it and double m_TargetScore; freeze
//   m_DrawColour.a at m_QuantumFlag; advance m_TimeElapsed and drive
//   m_AnimScale through two InverseSquareTransition eases (50->80 over
//   [0.03,0.33], then ->32 over [0.63,1.08]); past 0.63s also fly the board
//   (m_HudScale, pos) toward the on-screen clock via a Lerp against the
//   T_801() endpoint (unmapped .bss Vec3 -- see TODO below, cosmetic only);
//   past 1.08s bank the time award via TimeControl::AddTime(m_TargetScore)
//   and mark m_bPendingRemoval.
//   Always: top up the clock by dt while TimeControl::m_TimeRemaining < 1.0f,
//   and ease m_DisplayScore toward (m_TargetScore + 0.005f) by
//   pow(0.75, dt*60) per frame (double precision, matches binary).
void TimeSinkControl::Update(float dt) {
    TimeControl* tc = game_work.mCountDown;

    if (m_pPowerUp != 0) {
        // ASM @0x001c1bc0: ratchet m_DrawColour.a toward its running max, held
        // in m_QuantumFlag (uint8, not bool -- see header).
        if (m_DrawColour.a > m_QuantumFlag) {
            m_QuantumFlag = m_DrawColour.a;
        } else {
            m_DrawColour.a = m_QuantumFlag;
        }

        for (std::list<GameModifier*>::iterator it = m_pPowerUp->ModListBegin();
             it != m_pPowerUp->ModListEnd(); ++it) {
            GameModifier* mod = *it;
            if (mod->GetType() == 4) {
                m_TargetScore = static_cast<TimeSinkModifier*>(mod)->m_Accumulator;
                break;
            }
        }

        if (!m_JustActivated &&
            PowerUpManager::GetInstance()->GetActiveSingle(StringHash("score_mult"))) {
            m_JustActivated = true;
        }
    } else {
        if (m_JustActivated) {
            m_JustActivated = false;
            m_TargetScore *= 2.0f;
        }
        m_DrawColour.a = m_QuantumFlag;

        m_TimeElapsed += dt;

        float t = Clamp((m_TimeElapsed - 0.03f) / 0.3f, 0.0f, 1.0f);
        m_AnimScale = Lerp(50.0f, 80.0f, InverseSquareTransition(t, 0.0f));

        float t2 = Clamp((m_TimeElapsed - 0.63f) / 0.45f, 0.0f, 1.0f);
        m_AnimScale = Lerp(m_AnimScale, 32.0f, InverseSquareTransition(t2, 0.0f));

        if (m_TimeElapsed > 0.63f) {
            float t3 = Clamp((m_TimeElapsed - 0.63f) / 0.45f, 0.0f, 1.0f);
            float it3 = InverseSquareTransition(t3, 0.0f);

            // TODO: v1.6.1 DAT_002d928c/002d9290 -- T_801() Vec3(0,y,z) board
            // fly-in scale/pos endpoint (unmapped .bss global; read from HLE).
            // Placeholder Vec3(0,0,0) below. Cosmetic easing only -- does NOT
            // affect the time award (banked unconditionally at
            // m_TimeElapsed > 1.08f below).
            const _Vector3<float> T_801(0.0f, 0.0f, 0.0f);

            const _Vector3<float> hudScaleStart(0.0f, 0.25f, 1.0f);
            m_HudScale = hudScaleStart + (T_801 - hudScaleStart) * it3;

            // Function-local static matches the binary's magic-static guard
            // (ASM @0x001c1c00..0x001c1c28) -- computed once, first call to
            // reach this line. font[12] == game_work.pFontBlue2 (ASM-confirmed
            // @0x001c2020/0x001c2024 reads game_work+0x84).
            static const float s_TextOffset =
                game_work.pFontBlue2->MeasureString("+0:00") * 32.0f * 0.5f;

            // TC->size.x (NOT m_HudScale.x) -- ASM @0x001c1e80 reads
            // TimeControl+0x20, which is the inherited HUDControl::size field.
            float targetX = s_TextOffset + tc->size.x * 0.6f;
            _Vector3<float> posEnd = _Vector3<float>(targetX, 0.0f, 0.0f) - tc->pos;
            pos = T_801 + (posEnd - T_801) * it3;
        }

        if (m_TimeElapsed > 1.08f) {
            tc->AddTime(m_TargetScore);
            m_bPendingRemoval = 1;
        }
    }

    if (tc->m_TimeRemaining < 1.0f) {
        tc->AddTime(dt);
    }

    float goal = m_TargetScore + 0.005f;
    m_DisplayScore = (float)((double)goal +
        (double)(m_DisplayScore - goal) * pow(0.75, (double)(dt * 60.0f)));
}

// ASM-spec v1.6.1 TimeSinkControl::DrawOrder @0x001c1fb8:
//   HUDControl3d::Draw(hudScale) first (draws the shared ScreenEffect-stamped
//   texture); then formats "+M:SS" from m_DisplayScore (mins=(int)score,
//   secs=(int)((score-mins)*100)) and draws it with font[12]
//   (== game_work.pFontBlue2) at GetAdjustedPos() (== pos +
//   Vec3(480,320,0)*m_HudScale, via the shared T_799 helper -- NOT raw pos),
//   scale=m_AnimScale, yLineFactor=1.0, rotZ=0.0, alignment=0xF
//   (ASM @0x001c2078: mov r3,#0xf), maxWH=(0, DAT_002d92a4) (TODO below).
void TimeSinkControl::DrawOrder(float* hudScaleRaw, int /*layerMask*/) {
    HUDControl3d::Draw(hudScaleRaw);

    int mins = (int)m_DisplayScore;
    int secs = (int)((m_DisplayScore - (float)mins) * 100.0f);
    char buf[32];
    snprintf(buf, sizeof(buf), "+%i:%02i", mins, secs);

    Mortar::SmartPtr<Mortar::Font> font = game_work.pFontBlue2;
    if (!font.IsValid()) return;

    Mortar::Utf8StringIterator iter(buf);
    // TODO: v1.6.1 DAT_002d92a4 -- DrawOrder clip-rect .y (unmapped .bss
    // global; read from HLE). Placeholder 0.0f below. Cosmetic (word-wrap
    // limit) only.
    _Vector2<float> maxWH(0.0f, 0.0f);
    font->DrawString(m_AnimScale, 1.0f, 0.0f, iter, GetAdjustedPos(), m_DrawColour,
                      maxWH, 0xF, 0.0f);
}
