#ifndef FN_MISS_CONTROL_H
#define FN_MISS_CONTROL_H

//
// MissControl : HUDControl3d (size = 0x94)
// Overlay label that displays "critical" / "rare" / "X" text at a slice
// point. Binary keeps a 9-slot pool shared across all MissControl
// triggers. Same pool services Fruit critical/rare slices, Bomb zen-hit
// "X", and combo indicators (combo_3.tex .. combo_10.tex).
//
// Binary addresses:
//   ctor                     0x001511a0
//   dtor                     0x001513d8 / 0x00151468 / 0x001514f0
//   GetFree                  0x00150da4
//   MakeCritical             0x00151764
//   MakeRare                 0x001518d8
//   MakeDisappear            (part of GetFree family)
//   DrawQuadUnCached         0x0014b170
//
// Lifecycle:
//   1. GameInitialise constructs the 9-slot pool, which on first ctor
//      lazy-loads the 4 shared textures (critical.tex,
//      ultra_rare_plus_50.tex, hud_cross.tex, and combo_%d.tex for 3..10).
//   2. Make* picks a pool slot via GetFree, populates pos/texture/anim
//      state, sets m_bBusy = 1.
//   3. Update fades m_FadeAlpha to 0 over ~0.8s, then clears m_bBusy so
//      GetFree can re-use the slot.
//   4. HUD::Draw(0x200) renders each busy slot via HUDControl3d::Draw.
//

#include "HUDControl3d.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/Vec3.h"

class MissControl : public HUDControl3d {
public:
    // +0x30 (same offset as binary field_0x30): pool busy flag. 0 = slot
    // free and available to GetFree. 1 = slot active (fading). GetFree
    // round-robins over the pool looking for the first free slot.
    uint8_t m_bBusy;

    // +0x7c (inferred from MakeCritical decomp): combo indicator active
    // flag. 1 = render the combo counter overlay on top of the main
    // label. Currently set but not consumed by the port's Draw.
    uint8_t m_bComboActive;

    // +0x80: fade alpha (0..1). 0.808 (DAT_001518b8) set by
    // MakeCritical / MakeRare; fades to 0 over the Update loop.
    float m_FadeAlpha;

    // +0x84: animation state index. 3 = standard fade-in / hold /
    // fade-out. Port currently drives a simple linear fade.
    int m_AnimState;

    // +0x88: alpha scale multiplier. 1.0 for critical, 0.5 for rare.
    float m_AlphaScale;

    MissControl();
    ~MissControl() override;

    // One-time shared texture load. Must be called once at startup
    // before the pool is used. Loads critical.tex, ultra_rare_plus_50.tex,
    // hud_cross.tex.
    //
    // Binary does this lazily inside the first MissControl ctor (guarded
    // by a static ref count). Port calls it explicitly from
    // GameInitialise for clarity.
    static void LoadContent();

    // Allocate the static 9-slot pool (construction + HUD registration).
    // Call once from GameInitialise after LoadContent and after Game::hud
    // exists. Matches the binary's pool-ctor loop.
    static void AllocatePool();

    // 0x00150da4 — round-robin through the 9-slot pool returning the
    // next non-busy slot. If all slots are busy, returns the oldest
    // (the round-robin head) and the caller overwrites it.
    // Returns nullptr only if the pool hasn't been allocated yet.
    static MissControl* GetFree();

    // 0x00151764 — activate critical-hit label at a slice point.
    //   texture    = critical.tex
    //   m_FadeAlpha = 0.808
    //   m_AnimState = 3
    //   pos         = slicePos (screen-clamped to ±240 / ±160)
    //   m_bComboActive = 1
    //   m_bBusy = 1
    void MakeCritical(const Vec3& pos, int playerIdx);

    // 0x001518d8 — activate rare/special-fruit label. Same as MakeCritical
    // but texture = ultra_rare_plus_50.tex and m_AlphaScale = 0.5.
    void MakeRare(const Vec3& pos);

    // Zen-bomb "X" overlay and miss-penalty indicator. Caller supplies
    // the texture (hud_cross.tex for bomb hits, fruit portrait for miss)
    // and a size multiplier baked into field_0x34 (bit 0x200 in binary).
    // Matches the binary's MakeDisappear symbol (see
    // `_ZN11MissControl13MakeDisappearE...` at .rodata).
    void MakeDisappear(const Vec3& pos, int sizeMult,
                       const SmartPtr<Mortar::Texture>& tex);

    // HUDControl::Update override — advances the fade state machine and
    // clears m_bBusy when the fade completes so the slot returns to the
    // pool.
    void Update(float dt) override;

    // HUDControl::Draw override — renders the textured quad with
    // m_DrawColour.a scaled by m_FadeAlpha * m_AlphaScale when m_bBusy.
    // No-op while idle.
    void Draw(const Vec3& hudScale, int layerMask) override;
};

#endif
