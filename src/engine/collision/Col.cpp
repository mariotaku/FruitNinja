// Analysed: 2026-05-04T00:00
#include "collision/Col.h"

// Binary @ 0x0019fae8 -- clears m_CollideFlag; m_PrimaryPoint left for derived ctors
Col::Col() : m_PrimaryPoint(0.0f, 0.0f, 0.0f), m_CollideFlag(0) {}

