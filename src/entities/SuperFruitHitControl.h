#ifndef FN_SUPER_FRUIT_HIT_CONTROL_H
#define FN_SUPER_FRUIT_HIT_CONTROL_H

// SuperFruitHitControl — small helper entity spawned per combo-hit during
// super-fruit state. Only RemoveQuickly() @ 0x001bee10 is referenced by the
// binary's SuperFruitControl::Sliced path.
//
// Port: stub class preserving public API shape. Bodies are no-ops.

#include "Entity.h"

class SuperFruitHitControl : public Mortar::Entity {
public:
    SuperFruitHitControl();
    ~SuperFruitHitControl();

    void Update(float dt) override;
    void Draw(Renderer& r) override;
    void PostUpdate(float dt) override;

    // Binary @ 0x001bee10. Marks this entity for quick removal (sets kill flag).
    void RemoveQuickly();
};

#endif // FN_SUPER_FRUIT_HIT_CONTROL_H
