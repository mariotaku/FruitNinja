#ifndef FN_SUPER_FRUIT_HIT_CONTROL_H
#define FN_SUPER_FRUIT_HIT_CONTROL_H

// SuperFruitHitControl — small helper entity spawned per combo-hit during
// super-fruit state. Only RemoveQuickly() @ 0x001bee10 exists in the binary;
// there is no binary Update or Draw for this class.
//
// Port specific: no binary Update/Draw; only RemoveQuickly @ 0x001bee10 exists.
// The Update/Draw overrides below are no-ops to satisfy the vtable shape.

#include "Entity.h"

class SuperFruitHitControl : public Mortar::Entity {
public:
    SuperFruitHitControl();
    ~SuperFruitHitControl();

    // Port specific: no binary counterpart. No-op stubs to satisfy vtable.
    void Update(float dt) override;
    void Draw(Renderer& r) override;
    void PostUpdate(float dt) override;

    // Binary @ 0x001bee10. Lower-bound clamp: if m_field0x78 <= 0.8, set it to 0.8f.
    // (Despite the name it does NOT set a kill flag -- it floors the +0x78 float.)
    void RemoveQuickly();

    // +0x78 in binary SuperFruitHitControl. A float the binary lower-bound
    // clamps to 0.8 in RemoveQuickly() @ 0x001bee10. Caller SuperFruitControl::
    // Sliced @ 0x001bbd0c writes 0 to the adjacent +0x7c field, then calls
    // RemoveQuickly to clamp this one. Remaining subclass fields (binary +0x3c
    // .. +0x78, and +0x7c) are not yet ported -- stub class.
    float m_field0x78;
};

#endif // FN_SUPER_FRUIT_HIT_CONTROL_H
