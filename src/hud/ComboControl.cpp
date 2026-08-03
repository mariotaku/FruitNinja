// Defunct: ComboControl — unused in binary; class shape and vtable preserved
// per stub-don't-skip policy. Zero call sites in binary; combo popup is
// rendered by MissControl (binary @ 0x0017dad8).
// re-analyst confirmed 2026-05-20.
//
// Analysed: 2026-04-30T00:00

#include "ComboControl.h"
#include "../game/GameWork.h"
#include "../engine/render/Font.h"
#include <cstdio>
#include <cstring>

// ASM-verified: 2026-05-20 v1.6.1 ComboControl::ComboControl (re-analyst) -- format is "%i", not "x%d"
ComboControl::ComboControl(int comboCount)
    : m_Lifetime(1.0f), m_ComboCount(comboCount) {
    std::memset(m_Label, 0, sizeof(m_Label));
    std::snprintf(m_Label, sizeof(m_Label), "%i", comboCount);
    m_bNoDestructor = 0;
}

// dtor @ 0x00136c0c / 0x00136c4c / 0x00136c88
ComboControl::~ComboControl() {}

void ComboControl::Reset() {
    // no-op in binary @ 0x00136bdc
}

// Update @ 0x00136be4: lifetime -= dt; if lifetime < 0 set m_bPendingRemoval=1
void ComboControl::Update(float dt) {
    m_Lifetime -= dt;
    if (m_Lifetime < 0.0f) {
        m_bPendingRemoval = 1;
    }
}

// ASM-verified: 2026-05-20 v1.6.1 ComboControl::Init @ 0x00169370 (re-analyst) -- tail-calls Reset
void ComboControl::Init() { Reset(); }

// ASM-verified: 2026-05-20 v1.6.1 ComboControl::Release @ 0x00169388 (re-analyst) -- no-op
void ComboControl::Release() {}

// ASM-verified: 2026-05-20 v1.6.1 ComboControl::Skip @ 0x001693b0 (re-analyst) -- no-op
void ComboControl::Skip() {}

// ASM-verified: 2026-05-20 v1.6.1 ComboControl::PreDraw @ 0x001693ac (re-analyst) -- extra vtable slot, no-op
void ComboControl::PreDraw() {}

// ASM-verified: 2026-08-03T00:00Z v1.6.1 ComboControl::Draw @ 0x001695b0 (re-analyst)
// font=pFontNumbers, white, center, scale=30, no z/maxWH/rot/clip. Binary's Draw is the
// extra-vtable slot +0x40 no-args form; port wires the body inside the base
// Draw(hudScale,layerMask) override since no port-side caller hits the +0x40 slot.
// @0x001695d8 loads pFontNumbers (ldr r7,[r3,#0x5c]) straight into DrawString
// @0x00169644 -- no null test anywhere in the 41-insn body.
void ComboControl::Draw(float* /*hudScaleRaw*/) {
    Mortar::Utf8StringIterator iter(m_Label);
    Colour col(0xFF, 0xFF, 0xFF, 0xFF);
    game_work.pFontNumbers->DrawString(
        iter, col,
        Mortar::FONT_ALIGN_CENTER,
        pos.x, pos.y, 0.0f,
        30.0f,
        0.0f, 0.0f,
        0.0f,
        nullptr);
}
