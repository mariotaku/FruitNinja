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

// dtor (base/D2) @0x00169410, (complete/D1) @0x00169464, (deleting/D0) @0x001693b4
ComboControl::~ComboControl() {}

void ComboControl::Reset() {
    // no-op in binary @ 0x00169384
}

// Update @ 0x0016938c: lifetime -= dt; if lifetime < 0 set m_bPendingRemoval=1
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

// ASM-spec v1.6.1 ComboControl::Draw @0x001695b0 (re-analyst, no compile+diff)
// font = game_work.pM_Fonts[2] (+0x5c); colour = the Colour::White GLOBAL,
// byte-copied to a stack temp (port hardcodes 0xFFFFFFFF -- same value);
// align=1 (CENTER), scale=30.0f; z/maxWHx/maxWHy/rotZ are all PRESENT and
// 0.0f (one pool literal @0x00169658 replicated s2->s4/s5/s6); clip=NULL.
// Binary calls the by-VALUE-iterator DrawString wrapper @0x0024d6b8
// (ldmia r5,{r1,r2,r3}); the port calls the by-reference inner overload.
// Binary's Draw is the extra-vtable slot +0x40 no-args form; port wires the body inside the base
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
