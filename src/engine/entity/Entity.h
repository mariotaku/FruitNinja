#ifndef MORTAR_ENTITY_H
#define MORTAR_ENTITY_H

#include "math/Vec3.h"
#include <cstdint>

namespace Mortar {

// Entity flags (at offset +0x0C byte 3)
enum EntityFlags {
    ENTITY_INACTIVE        = 0x01,
    ENTITY_UPDATING        = 0x04,
    ENTITY_POST_UPDATING   = 0x08,
    ENTITY_PENDING_DEACTIVATE = 0x10,
    ENTITY_NO_DESTRUCT     = 0x20
};

// Matches original MortarEntity base class (0x3C bytes)
// Vtable order: ~dtor, ~dtor, OnActivate, OnDeactivate, Update, Draw, PostUpdate
class Entity {
public:
    Vec3 pos;           // position
    Vec3 vel;           // velocity
    float angle;        // rotation angle
    float scale;        // scale factor
    uint8_t flags;      // EntityFlags bitmask
    uint8_t m_EntityType; // entity type (0=Fruit, 1=Bomb, etc.)

    Entity();
    virtual ~Entity();

    virtual void OnActivate() {}
    virtual void OnDeactivate() {}
    virtual void Update(float dt) { (void)dt; }
    virtual void Draw() {}
    virtual void PostUpdate(float dt) { (void)dt; }

    bool IsActive() const { return (flags & ENTITY_INACTIVE) == 0; }
    void MarkForDeactivation() { flags |= ENTITY_PENDING_DEACTIVATE; }
};

} // namespace Mortar

#endif
