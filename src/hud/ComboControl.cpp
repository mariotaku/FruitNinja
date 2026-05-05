// Analysed: 2026-04-30T00:00

#include "ComboControl.h"
#include <cstdio>
#include <cstring>

// ctor @ 0x00136cc4
// Sets vtable, formats label via OS_SPrintf(buf, 8, fmt, comboCount), lifetime=1.0f.
ComboControl::ComboControl(int comboCount)
    : m_Lifetime(1.0f), m_ComboCount(comboCount) {
    std::memset(m_Label, 0, sizeof(m_Label));
    std::snprintf(m_Label, sizeof(m_Label), "x%d", comboCount);
    m_bNoDestructor = 0;
}

// dtor @ 0x00136c0c / 0x00136c4c / 0x00136c88
ComboControl::~ComboControl() {}

void ComboControl::Reset() {
    // STUB: ComboControl::Reset -- binary @ 0x00136bdc (no-op in binary)
}

// Update @ 0x00136be4: lifetime -= dt; if lifetime < 0 set m_bPendingRemoval=1
void ComboControl::Update(float dt) {
    m_Lifetime -= dt;
    if (m_Lifetime < 0.0f) {
        m_bPendingRemoval = 1;
    }
}

void ComboControl::Init() {
    // STUB: ComboControl::Init -- binary @ 0x???? (TODO RE)
}

void ComboControl::PreDraw() {
    // STUB: ComboControl::PreDraw -- binary @ 0x???? (TODO RE)
}

void ComboControl::Release() {
    // STUB: ComboControl::Release -- binary @ 0x???? (TODO RE)
}

void ComboControl::Skip() {
    // STUB: ComboControl::Skip -- binary @ 0x???? (TODO RE)
}
