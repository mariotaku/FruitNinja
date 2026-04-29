// Analysed: 2026-04-30T00:00

#include "SpeedControl.h"
#include <cstring>

// ctor @ 0x0016133c
// Stub: zero-fills subclass fields; real ctor loads localised speed gauge texture.
SpeedControl::SpeedControl() {
    std::memset(m_fields, 0, sizeof(m_fields));
}

// dtor @ 0x00161558 / 0x001615d4 / 0x00161650
SpeedControl::~SpeedControl() {}
