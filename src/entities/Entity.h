#ifndef FN_ENTITY_H
#define FN_ENTITY_H

#include "math/Vec3.h"
#include "collision/ColSphere.h"
#include <cstdint>

struct Renderer;

// Matches Mortar::Entity base class (0x3c bytes)
class Entity {
public:
    // +0x0c: flags (bit 0x01 = has collision, bit 0x10 = skip/hidden, bit 0x11 = pre-allocated)
    uint8_t flags;

    // +0x10..+0x18: position
    Vec3 pos;

    // +0x1c..+0x24: velocity
    Vec3 vel;

    // +0x28..+0x30: scale (visual size)
    Vec3 scale;

    // Entity type (0=Fruit, 1=Bomb, 4=BombBlast)
    int entityType;

    // Active state
    bool active;

    // Collision sphere. Radius = 0 means "no collision" (default for
    // entities that don't participate in blade collision, e.g. BombBlast).
    // Matches the binary's +0x38 m_Col pointer, inlined as a value for
    // port simplicity.
    Mortar::ColSphere m_Col;

    Entity() : flags(0), entityType(0), active(false) {}
    virtual ~Entity() {}

    virtual void Init(int param1, int param2, int param3) { (void)param1; (void)param2; (void)param3; }
    virtual void Update(float dt) { (void)dt; }
    virtual void Draw(Renderer& r) { (void)r; }
    virtual void Deactivate() { active = false; }

    // Matches the CollisionResponse vtable slot (0x0017280c for Bomb,
    // Fruit equivalent for fruit). Called when the blade collides with
    // this entity's collision sphere. Default: no-op.
    virtual void OnSliced(const Vec3& bladeVel) { (void)bladeVel; }

    bool IsActive() const { return active && !(flags & 0x10); }
};

#endif
