#ifndef FN_MISS_CONTROL_H
#define FN_MISS_CONTROL_H

//
// MissControl : HUDControl3d (size = 0x94)
// Overlay label that displays "critical"/"rare" text or combo indicators
// at a slice point. Binary keeps a 9-slot pool.
//
// Binary addresses (for future RE / full implementation):
//   GetFree        @ 0x00150da4
//   MakeCritical   @ 0x00151764
//   MakeRare       @ 0x001518d8
//   DrawQuad       @ 0x0014b170
//   ctor preloads overlay textures + combo_%d.tex for indices 3..10
//
// Port status: STUB — call sites (Fruit critical/rare slice, Bomb zen hit,
// Fruit miss penalty) dispatch through these methods but the overlay
// doesn't render yet. See docs/entities/miss-control.md for full spec.
//

#include "HUDControl3d.h"
#include "math/Vec3.h"

class MissControl : public HUDControl3d {
public:
    // +0x80 — fade alpha (0..1). 0.808 = DAT_001518b8 on MakeCritical.
    float m_FadeAlpha;

    // +0x84 — animation state index. 3 = standard fade-in path.
    int m_AnimState;

    // +0x88 — alpha scale; 0.5 used by MakeRare (half-opacity).
    float m_AlphaScale;

    MissControl() : m_FadeAlpha(0.0f), m_AnimState(0), m_AlphaScale(1.0f) {}

    // 0x00150da4 — find next free slot in the pool. Stub returns nullptr
    // until the 9-instance pool is allocated by GameInitialise.
    static MissControl* GetFree();

    // 0x00151764 — activate critical-hit label at a slice point.
    //   m_FadeAlpha = 0.808, m_AnimState = 3, pos = slicePos (screen-clamped).
    void MakeCritical(const Vec3& pos, int playerIdx);

    // 0x001518d8 — activate rare-hit label. Same setup as MakeCritical but
    // m_AlphaScale = 0.5 so the overlay renders at half-opacity.
    void MakeRare(const Vec3& pos);

    // Disappear / bomb-miss variant used by Bomb::OnSliced zen branch. Takes
    // a pos, size multiplier (field_0x34), and a texture to render.
    // Binary addr TBD — port-stubbed until the full MissControl pool lands.
    void MakeDisappear(const Vec3& pos, int sizeMult, unsigned int tex);
};

#endif
