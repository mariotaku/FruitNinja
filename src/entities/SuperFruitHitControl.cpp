// SuperFruitHitControl — binary @ 0x001bee10 (RemoveQuickly only).
// Port specific: no binary Update/Draw; only RemoveQuickly @ 0x001bee10 exists.

#include "SuperFruitHitControl.h"

SuperFruitHitControl::SuperFruitHitControl()
    : m_field0x78(0.0f)
{
    entityType = 6;  // super-fruit entity type (binary type 6)
}

SuperFruitHitControl::~SuperFruitHitControl()
{
}

// Port specific: no binary counterpart for Update.
void SuperFruitHitControl::Update(float /*dt*/)
{
}

// Port specific: no binary counterpart for Draw.
void SuperFruitHitControl::Draw(Renderer& /*r*/)
{
}

void SuperFruitHitControl::PostUpdate(float /*dt*/)
{
}

void SuperFruitHitControl::RemoveQuickly()
{
    // Binary @ 0x001bee10: lower-bound clamp of m_field0x78 to 0.8.
    // asm: vldr s15,[r0,#0x78]; vcvt.f64.f32 d17,s15; vldr.64 d16,=0.8(double);
    //      vldr.32 s14,=0.8f; vcmpe.f64 d17,d16; vmovle.f32 s15,s14; vstr.32 s15,[r0,#0x78]
    // DAT_001bee38 = 0.8 (double, 0x3FE9999999999999) -- the compare threshold.
    // DAT_001bee40 = 0.8f (float, 0x3F4CCCCD) -- the clamped-to value.
    // The compare is done in double precision, so the <= test uses the widened value.
    if ((double)m_field0x78 <= 0.8) {
        m_field0x78 = 0.8f;
    }
}
