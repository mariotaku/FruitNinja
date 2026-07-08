#ifndef FN_HUD_TIME_SINK_CONTROL_H
#define FN_HUD_TIME_SINK_CONTROL_H

// TimeSinkControl : HUDControl3d (size = 0x98)
// Berry-Blast time-award board -- the "defer=time" sibling of
// ScoreMultiplyerBoard (defer=points). Shown while the owning "score_mult"-ish
// PowerUp is active (mirrors its TimeSinkModifier accumulator), then
// self-animates a fly-in to the on-screen clock and banks the accumulated
// time via TimeControl::AddTime once detached.
//
// Lifecycle: ScreenEffect::Activate (kind==2, XML defer="time") constructs
// this and sets m_pPowerUp to the owning PowerUp. ScreenEffect::Deactivate
// clears m_pPowerUp (switching this into the self-animating RELEASE phase);
// if the window was aborted early (owner's GetCurrentTimeProgress() > 0.01f),
// it also zeroes m_TargetScore (no payout).
//
// v1.6.1 TimeSinkControl ctor @0x001c19dc, Update @0x001c1b98, DrawOrder @0x001c1fb8
// ASM-spec v1.6.1 TimeSinkControl::TimeSinkControl/Update/DrawOrder
// @ 0x001c19dc / 0x001c1b98 / 0x001c1fb8 (implementer, GhidraMCP decompile+disasm read)

#include "HUDControl3d.h"

class PowerUp;

class TimeSinkControl : public HUDControl3d {
public:
    // +0x7c: number displayed by DrawOrder ("+M:SS"), eased toward m_TargetScore
    // each frame: goal = m_TargetScore + 0.005f; m_DisplayScore = goal +
    // (m_DisplayScore - goal) * powf(0.75f, dt*60.0f).
    float m_DisplayScore;

    // +0x80: pending time award (seconds). While m_pPowerUp is set, mirrors the
    // owning PowerUp's TimeSinkModifier::m_Accumulator (GetType()==4). On
    // release, doubled once if m_JustActivated was latched; banked via
    // TimeControl::AddTime() once m_TimeElapsed > 1.08f.
    float m_TargetScore;

    // +0x84: written 0 by ctor; no read/write site in Update or DrawOrder
    // (exhaustive ASM scan of both functions). Reserved.
    float m_unused84;  // purpose unknown

    // +0x88: self-animation clock; starts at 0 the frame m_pPowerUp is cleared
    // (RELEASE phase entry).
    float m_TimeElapsed;

    // +0x8c: DrawOrder's font scale. Ctor default 50.0f (matches the RELEASE
    // phase's first InverseSquareTransition ease target range).
    float m_AnimScale;

    // +0x90: latched when PowerUpManager has an active "score_mult" power
    // while m_pPowerUp is set. Consumed exactly once on RELEASE entry: doubles
    // m_TargetScore, then clears.
    bool m_JustActivated;

    // +0x91: ASM-confirmed @0x001c1bc0/0x001c1c78 as a uint8 alpha-ratchet
    // (ldrb/strb compared unsigned against m_DrawColour.a, values up to 255),
    // NOT a boolean despite the "Quantum flag"-shaped name -- holds the
    // highest m_DrawColour.a seen while active, then freezes alpha there.
    uint8_t m_QuantumFlag;

    uint8_t _pad92[2];

    // +0x94: owning PowerUp while the time-defer window is active; cleared by
    // ScreenEffect::Deactivate (kind==2).
    PowerUp* m_pPowerUp;

    TimeSinkControl();
    ~TimeSinkControl() override {}

    // v1.6.1 TimeSinkControl::Update @0x001c1b98
    void Update(float dt) override;

    // v1.6.1 TimeSinkControl::DrawOrder @0x001c1fb8
    void DrawOrder(float* hudScale, int layerMask) override;
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(TimeSinkControl, m_DisplayScore) == 0x7c, "TimeSinkControl::m_DisplayScore @ +0x7c");
static_assert(offsetof(TimeSinkControl, m_TargetScore)  == 0x80, "TimeSinkControl::m_TargetScore @ +0x80");
static_assert(offsetof(TimeSinkControl, m_unused84)     == 0x84, "TimeSinkControl::m_unused84 @ +0x84");
static_assert(offsetof(TimeSinkControl, m_TimeElapsed)  == 0x88, "TimeSinkControl::m_TimeElapsed @ +0x88");
static_assert(offsetof(TimeSinkControl, m_AnimScale)    == 0x8c, "TimeSinkControl::m_AnimScale @ +0x8c");
static_assert(offsetof(TimeSinkControl, m_JustActivated)== 0x90, "TimeSinkControl::m_JustActivated @ +0x90");
static_assert(offsetof(TimeSinkControl, m_QuantumFlag)  == 0x91, "TimeSinkControl::m_QuantumFlag @ +0x91");
static_assert(offsetof(TimeSinkControl, m_pPowerUp)     == 0x94, "TimeSinkControl::m_pPowerUp @ +0x94");
static_assert(sizeof(TimeSinkControl) == 0x98, "TimeSinkControl size mismatch"); // v1.6.1 TimeSinkControl @0x001c19dc
#endif

#endif // FN_HUD_TIME_SINK_CONTROL_H
