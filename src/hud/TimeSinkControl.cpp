#include "TimeSinkControl.h"

// TODO: v1.6.1 0x001c19dc (TimeSinkControl) — port Update @0x001c1b98 / DrawOrder @0x001c1fb8
// Ctor zero-inits the unresolved state block; class otherwise inherits
// HUDControl3d's Update/Draw (no-op stub for the time-sink-specific behaviour).
TimeSinkControl::TimeSinkControl()
    : HUDControl3d()
    , m_Field80(0)
    , m_pOwner(0)
{
    m_Reserved7C[0] = m_Reserved7C[1] = m_Reserved7C[2] = m_Reserved7C[3] = 0;
    for (int i = 0; i < 0x10; ++i) m_Reserved84[i] = 0;
}
