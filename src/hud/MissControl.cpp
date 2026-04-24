#include "MissControl.h"

// Port stub: 9-instance MissControl pool not yet allocated.
// Returns nullptr so callers (Fruit::Slice critical path, Bomb zen-hit,
// miss-penalty path) fall through gracefully. Full implementation will
// need GameInitialise to construct the 9 slots and register them with
// the HUD.
MissControl* MissControl::GetFree() {
    return nullptr;
}

// Stub bodies match the documented binary behaviour but do not render.
// When the overlay pool + ctor (loads combo_%d.tex for indices 3..10)
// lands, these functions should populate m_FadeAlpha / m_AnimState /
// m_AlphaScale and screen-clamp pos. See docs/entities/miss-control.md.
void MissControl::MakeCritical(const Vec3& /*pos*/, int /*playerIdx*/) {
    // TODO: pos clamp to ±240 X / ±160 Y via half-size from m_Texture.
    // m_FadeAlpha = 0.808f;
    // m_AnimState = 3;
}

void MissControl::MakeRare(const Vec3& pos) {
    MakeCritical(pos, 0);
    m_AlphaScale = 0.5f;
}

void MissControl::MakeDisappear(const Vec3& /*pos*/, int /*sizeMult*/, unsigned int /*tex*/) {
    // TODO: same clamp + state setup as MakeCritical with the provided
    // texture and size multiplier stored on the overlay.
}
