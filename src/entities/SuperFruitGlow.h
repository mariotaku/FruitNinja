#ifndef FN_SUPER_FRUIT_GLOW_H
#define FN_SUPER_FRUIT_GLOW_H

//
// SuperFruitGlow : HUDControl3d — glow halo attached to a host Fruit during super-fruit state.
// Binary: ctor @ 0x001c06bc (takes Fruit*), dtor @ 0x1c02b4/0x1c0258.
//   Calls HUDControl3d::HUDControl3d(this, fruit) @ 0x102c0c, own vptr=GOT+8.
//   vtable @ 0x2CE3E8 (typeinfo 0x2CE428).
//   sizeof(SuperFruitGlow): 0x8C (confirmed via SuperFruitControl `new 0x8c` allocation)
//
// Layout: HUDControl3d base is 0x7C bytes. SuperFruitGlow own fields @ +0x7C..+0x8B.
// Fields from spec at offsets +0x08..+0x78 are absolute offsets into the inherited
// HUDControl3d (HUDControl) layout, aliased/renamed for SuperFruitGlow semantics:
//   +0x08: HUDControl::pos       (Vec3 m_Pos)
//   +0x14: HUDControl::m_HudScale (Vec3 m_BaseScale)
//   +0x28: HUDControl::m_Timer   (float m_Spin — += dt*k each Update; DrawOrder mirrors)
//   +0x2c: (float m_SpinDraw — second spin slot; DrawOrder flips sign)
//   +0x30: HUDControl::m_Active  (ctor sets 0x80? — actually m_LayerFlags at +0x34)
//   +0x33: HUDControl::m_bPendingRemoval (m_Dead — set 1 when fade done)
//   +0x5c: HUDControl::m_DrawColour (Colour m_Colour — alpha = k * m_FadeAlt)
//   +0x74: HUDControl::m_bUseHUDScales (u8; ctor default=1)
//
// Vtable slot overrides:
//   slot3  Release         @ 0x1c01c8
//   slot9  DrawOrder       @ 0x1bfb18  (double-draw spin mirror)
//   slot10 Update          @ 0x1c0024  (+-2*dt fade + Fruit tracking + looping SFX handle)
//   slot11 SetToMultiplayerState @ 0x1bffb8
//

#include "hud/HUDControl3d.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include <cstdint>

class Fruit;
namespace Mortar { class MortarSound; }

class SuperFruitGlow : public HUDControl3d {
public:
    // Inherited HUDControl3d is 0x7C bytes. Own fields follow at +0x7C.

    // +0x7C: pointer to tracked host fruit. Cleared when sliced or dead.
    Fruit* m_pFruit;                                // +0x7C (binary +0x7c)

    // +0x80: looping SFX handle. SFXPlay in ctor; Released on fade-out.
    Mortar::MortarSound* m_pSound;                  // +0x80 (binary +0x80)

    // +0x84: fade progress. Grows 2*dt toward 1.0; on slice decays -2*dt toward 0.
    float m_Fade;                                   // +0x84 (binary +0x84)

    // +0x88: glow ramp (ctor sets DAT_001c0854). Used in alpha/scale computation.
    float m_FadeAlt;                                // +0x88 (binary +0x88)

    // ctor @ 0x001c06bc — takes host Fruit*; subscribes to fruit-slice event;
    // plays looping SFX; registers glow.
    explicit SuperFruitGlow(Fruit* fruit);

    // dtor @ 0x1c02b4
    virtual ~SuperFruitGlow();

    // slot3: Release @ 0x1c01c8. Unsubscribes from fruit-slice event.
    void Release() override;

    // slot9: DrawOrder @ 0x1bfb18. Double-draw spin mirror for two-blade glow.
    void DrawOrder(const Vec3& hudScale, int layerMask) override;

    // slot10: Update @ 0x1c0024. Fade in/out, pos tracking, SFX volume.
    void Update(float dt) override;

    // slot11: SetToMultiplayerState @ 0x1bffb8. Calls Release then HUDControl::SetToMultiplayerState.
    bool SetToMultiplayerState() override;

    // Non-virtual helper (used from SuperFruitControl when fruit is sliced).
    // Sets m_Sliced flag so Update can decay the fade.
    void SetSliced() { m_bPendingRemoval = 0; /* set glow-decay path */ }
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(SuperFruitGlow)                    == 0x8C, "SuperFruitGlow sizeof wrong (binary 0x8C)");
static_assert(offsetof(SuperFruitGlow, m_pFruit)        == 0x7C, "SuperFruitGlow::m_pFruit offset wrong");
static_assert(offsetof(SuperFruitGlow, m_pSound)        == 0x80, "SuperFruitGlow::m_pSound offset wrong");
static_assert(offsetof(SuperFruitGlow, m_Fade)          == 0x84, "SuperFruitGlow::m_Fade offset wrong");
static_assert(offsetof(SuperFruitGlow, m_FadeAlt)       == 0x88, "SuperFruitGlow::m_FadeAlt offset wrong");
#endif

#endif // FN_SUPER_FRUIT_GLOW_H
