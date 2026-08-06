#ifndef FN_SUPER_FRUIT_GLOW_H
#define FN_SUPER_FRUIT_GLOW_H

//
// SuperFruitGlow : HUDControl3d — glow halo attached to a host Fruit during super-fruit state.
// Binary: ctor @ 0x001c06bc (takes Fruit*), dtor @ 0x1c02b4/0x1c0258.
//   Calls HUDControl3d::HUDControl3d(this, fruit) @ 0x102c0c, own vptr=GOT+8.
//   vtable @ 0x2CE3E8 (typeinfo 0x2CE428).
//   sizeof(SuperFruitGlow): 0x8C (confirmed via SuperFruitControl `new 0x8c` allocation)
//
// ASM-spec v1.6.1 SuperFruitGlow::SuperFruitGlow(Fruit*) @0x001c06bc
// (prior ASM-verified stamp here was contradicted by #137 triage -- the ctor
// was missing m_Texture/size/m_Timer inits; no compile+diff has been run
// since the fix, so this is not yet re-stamped ASM-verified).
//
// Layout: HUDControl3d base is 0x7C bytes. SuperFruitGlow own fields @ +0x7C..+0x8B.
// Fields from spec at offsets +0x08..+0x78 are absolute offsets into the inherited
// HUDControl3d (HUDControl) layout, aliased/renamed for SuperFruitGlow semantics:
//   +0x08: HUDControl::pos       (Vec3 m_Pos)
//   +0x14: HUDControl::m_HudScale (Vec3 m_BaseScale)
//   +0x20: HUDControl::size      (Vec3; ctor sets = _Vector3<float>::One * 150.0f)
//   +0x2c: HUDControl::m_Timer   (float; ctor sets = random phase in [10,170);
//          Update += dt*60; DrawOrder mirrors sign for the second blade)
//   +0x30: HUDControl::m_Active  (ctor sets 0x80? — actually m_LayerFlags at +0x34)
//   +0x33: HUDControl::m_bPendingRemoval (m_Dead — set 1 when fade done)
//   +0x5c: HUDControl::m_DrawColour (Colour m_Colour — alpha = trunc(75 * m_Fade))
//   +0x74: HUDControl3d::m_Texture (SmartPtr<Texture>; ctor sets = SuperFruitGlow::GlowTexture)
//
// Own field map (ASM-verified from Update @ 0x1c0024):
//   +0x7c: byte  m_bSliced    — set when host fruit is sliced; drives fade-out path
//   +0x80: Fruit* m_pFruit    — tracked host fruit pointer
//   +0x84: MortarSound* m_pSound — looping SFX handle (SFXPlay in ctor; Released on fade-out)
//   +0x88: float m_Fade       — fade accumulator [0..1]; drives alpha AND sound volume
//
// Vtable slot overrides:
//   slot3  Release         @ 0x1c01c8
//   slot9  DrawOrder       @ 0x1bfb18  (double-draw spin mirror)
//   slot10 Update          @ 0x1c0024  (+-2*dt fade + Fruit tracking + looping SFX handle)
//   slot11 SetToMultiplayerState @ 0x1bffb8
//

#include "hud/HUDControl3d.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include <cstdint>

class Fruit;
namespace Mortar { class MortarSound; }

class SuperFruitGlow : public HUDControl3d {
public:
    // Glow halo texture ("rays.tex"), loaded by SuperFruitControl::LoadContent
    // @0x001bda74 (LoadLocalisedTexture) and assigned to m_Texture in the ctor below.
    static Mortar::SmartPtr<Mortar::Texture> GlowTexture;

    // Inherited HUDControl3d is 0x7C bytes. Own fields follow at +0x7C.

    // +0x7c: set when host fruit is sliced; drives fade-out path in Update.
    uint8_t m_bSliced;                               // +0x7C (binary +0x7c)

    uint8_t _pad7d[3];                               // +0x7D alignment

    // +0x80: pointer to tracked host fruit. Cleared when fruit is killed.
    Fruit* m_pFruit;                                 // +0x80 (binary +0x80)

    // +0x84: looping SFX handle. SFXPlay in ctor; Released when fade reaches 0.
    Mortar::MortarSound* m_pSound;                   // +0x84 (binary +0x84)

    // +0x88: fade accumulator [0..1]. +=2*dt when not sliced; -=2*dt when sliced.
    // Drives both alpha (trunc(75*m_Fade)) and sound volume.
    float m_Fade;                                    // +0x88 (binary +0x88)

    // ctor @ 0x001c06bc — takes host Fruit*; subscribes to fruit-killed event;
    // plays looping SFX; registers glow.
    explicit SuperFruitGlow(Fruit* fruit);

    // dtor @ 0x1c02b4
    virtual ~SuperFruitGlow();

    // slot3: Release @ 0x1c01c8. Unsubscribes from fruit-killed event.
    void Release() override;

    // slot9: DrawOrder @ 0x1bfb18. Double-draw spin mirror for two-blade glow.
    void DrawOrder(float* hudScaleRaw, int layerMask) override;

    // slot10: Update @ 0x1c0024. Fade in/out, pos tracking, SFX volume.
    void Update(float dt) override;

    // slot11: SetToMultiplayerState @ 0x1bffb8. Calls Release then HUDControl::SetToMultiplayerState.
    bool SetToMultiplayerState() override;

    // Called when the tracked fruit fires m_OnKilled (binary @ 0x1c06bc wiring).
    // Clears m_pFruit so Update/Release no longer reference the dead entity.
    void OnFruitKilled(Fruit* fruit);
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(SuperFruitGlow)                    == 0x8C, "SuperFruitGlow sizeof wrong (binary 0x8C)");
static_assert(offsetof(SuperFruitGlow, m_bSliced)       == 0x7C, "SuperFruitGlow::m_bSliced offset wrong");
static_assert(offsetof(SuperFruitGlow, m_pFruit)        == 0x80, "SuperFruitGlow::m_pFruit offset wrong");
static_assert(offsetof(SuperFruitGlow, m_pSound)        == 0x84, "SuperFruitGlow::m_pSound offset wrong");
static_assert(offsetof(SuperFruitGlow, m_Fade)          == 0x88, "SuperFruitGlow::m_Fade offset wrong");
#endif

#endif // FN_SUPER_FRUIT_GLOW_H
