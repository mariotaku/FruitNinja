#ifndef FN_HUD_TIME_SINK_CONTROL_H
#define FN_HUD_TIME_SINK_CONTROL_H

// TimeSinkControl : HUDControl3d (size = 0x98)
// Berry-Blast time-defer board -- the "defer=time" sibling of
// ScoreMultiplyerBoard (defer=points). LIVE in v1.6.1 (ScreenEffect::Activate/
// Deactivate kind==2 have real call sites for it) but Update/DrawOrder are
// NOT yet RE'd/ported -- this class is a visible-shape STUB: the class,
// vtable, and owner field exist so ScreenEffect's kind-2 dispatch compiles
// and stays binary-faithful, but it has no self-animation logic of its own
// (inherits HUDControl3d::Update/Draw, so it still renders the shared
// texture/pos/size ScreenEffect::Activate stamps onto it).
//
// TODO: v1.6.1 0x001c19dc (TimeSinkControl) — port Update @0x001c1b98 / DrawOrder @0x001c1fb8

#include "HUDControl3d.h"

class PowerUp;

class TimeSinkControl : public HUDControl3d {
public:
    // +0x7c..+0x7f: unresolved -- part of the unported Update/DrawOrder state.
    uint8_t m_Reserved7C[4];
    // +0x80: unresolved state field; ScreenEffect::Deactivate (kind==2) clears
    // this to 0 when the owning PowerUp's GetCurrentTimeProgress() > 0.01f.
    uint32_t m_Field80;
    // +0x84..+0x93: unresolved -- part of the unported Update/DrawOrder state.
    uint8_t m_Reserved84[0x10];
    // +0x94: owning PowerUp while the time-defer window is active; cleared by
    // ScreenEffect::Deactivate (kind==2).
    PowerUp* m_pOwner;

    TimeSinkControl();
    ~TimeSinkControl() override {}
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(TimeSinkControl, m_Field80) == 0x80, "TimeSinkControl::m_Field80 @ +0x80");
static_assert(offsetof(TimeSinkControl, m_pOwner)  == 0x94, "TimeSinkControl::m_pOwner @ +0x94");
static_assert(sizeof(TimeSinkControl) == 0x98, "TimeSinkControl size mismatch"); // v1.6.1 TimeSinkControl @0x001c19dc
#endif

#endif // FN_HUD_TIME_SINK_CONTROL_H
