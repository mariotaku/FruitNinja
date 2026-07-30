#ifndef FN_HUD_TIME_CONTROL_H
#define FN_HUD_TIME_CONTROL_H

// TimeControl : HUDControl3d (size = 0x108)
// Countdown timer for Arcade and Zen modes.
// Binary: ctor 0x001622e8, Update 0x001624a4, Draw 0x001628d8
//
// Analysed: 2026-04-30T12:00

#include "HUDControl3d.h"

class TimeControl : public HUDControl3d {
public:
    // +0x7C: live countdown value (seconds)
    float m_TimeRemaining;
    // +0x80: formatted text buffer — OS_SPrintf("%i:%02i", min, sec)
    char  m_TextBuffer[64];
    // +0xC0: initial seconds; -1.0 sentinel = not configured
    float m_CountdownStart;
    // +0xC4: slow-clock shimmer accumulator: ((elapsed_seconds % 6) + 0.5f).
    // Written every frame in Update (both Zen and Arcade paths).
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x001627e2 (re-analyst)
    // No port-side reader exists yet -- the binary's Draw reads it for a
    // shimmer animation that the port's simplified Draw omits. Kept as a
    // write-only field so the layout matches binary; can be wired into
    // Draw if/when the shimmer is restored.
    float m_SlowClockPhase;
    // +0xC8: "+N" powerup overlay text; [0]==0 means hide
    char  m_PowerupOverlay[64];

    // ctor: no parameters (0x001622e8)
    TimeControl();
    ~TimeControl() override {}

    // vtable overrides
    void  Init() override;
    void  Release() override;
    void  Reset() override;
    void  Update(float dt) override;
    void  Draw(float* hudScaleRaw) override;
    bool  SetToMultiplayerState() override;
    int   GetType() override { return 4; }
    void  Skip() override;

    // Binary: 0x001620f0
    void  CountDown(float startSeconds);
    // Binary: 0x00162134
    float GetCountDown() const;
    void  AddTime(float delta);

private:
    bool IsTimedGame() const;
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(TimeControl) == 0x108, "TimeControl size mismatch"); // v1.6.1 GameInit @0x001ce558 -- operator new(0x108) sizes TimeControl
#endif

#endif
