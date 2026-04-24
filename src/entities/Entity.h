#ifndef FN_ENTITY_H
#define FN_ENTITY_H

#include "math/Vec3.h"
#include "collision/ColSphere.h"
#include <cstdint>

struct Renderer;

// Mortar::Entity base class (~0x3c bytes in binary).
//
// Flag byte lives at Entity+0x0c. The binary encodes active / scheduled-
// for-deactivation / in-flight-update state in these bits — there is no
// separate `active` boolean. Keeping the port in sync with the bit layout
// is required for ActorManager::Update and Draw to gate iteration the
// same way the binary does (test `(flags & 0x11) == 0`).
//
// Analysed: 2026-04-23T01:00
enum EntityFlagBits : uint8_t {
    ENT_INACTIVE      = 0x01,  // cleared by Entity::Activate on pool recycle
    ENT_UPDATING      = 0x04,  // set while ActorManager::Update calls vtable
    ENT_POST_UPDATING = 0x08,  // set while PostUpdate runs
    ENT_KILLED        = 0x10,  // entity wants to retire; swept by Update
    ENT_NO_DESTRUCT   = 0x20,  // ActorManager::Remove skips delete if set
    // 0x11 is the combined "skip" mask — an entity is processed only when
    // `(flags & 0x11) == 0`.
    ENT_SKIP_MASK     = ENT_INACTIVE | ENT_KILLED,
};

// Port struct — mirrors the binary offsets for fields that callers touch.
// Exact layout isn't load-bearing for the port (we don't cast from raw
// memory) but the named offsets keep the Ghidra cross-reference obvious.
class Entity {
public:
    // +0x0c: flag byte (see enum above).
    uint8_t flags;

    // +0x10..+0x18: position
    Vec3 pos;

    // +0x1c..+0x24: velocity
    Vec3 vel;

    // +0x28..+0x30: scale (visual size)
    Vec3 scale;

    // +0x34: recycle-state byte. Zeroed by ActorManager::Add on the
    // factory path (never on the recycle path). Port tracks it to stay
    // bit-faithful with the binary; no port code reads it yet.
    uint8_t m_RecycleFlag;

    // +0x35: entity type byte in the binary. Port widens to int since
    // gameplay code indexes with regular ints; value range is still 0..4.
    int entityType;

    // +0x38: collision sphere (binary: m_Col pointer; port inlines).
    // Radius == 0 means "no collision" (e.g. BombBlast).
    Mortar::ColSphere m_Col;

    Entity() : flags(0), m_RecycleFlag(0), entityType(0) {}
    virtual ~Entity() {}

    virtual void Init(int, int, int) {}
    virtual void Update(float) {}

    // Vtable slot 6 (+0x18). Binary calls this from ActorManager::Update
    // right after Update, still under the gate `(flags & 0x11) == 0`.
    // Named "DrawUpdate" in the per-subclass docs (bomb.md) but semantically
    // a PostUpdate hook — runs once per tick between Update and the
    // deactivation sweep. Bomb uses it to sync its fuse-emitter position
    // with the rotation that Update just advanced.
    virtual void PostUpdate(float) {}

    virtual void Draw(Renderer&) {}

    // Port's Deactivate is a cleanup callback, invoked by
    // ActorManager::Deactivate before the entity returns to the free pool.
    // Base implementation marks the entity inactive; subclasses override
    // to drop particle emitters / external references.
    virtual void Deactivate() { flags |= ENT_INACTIVE; }

    // Matches the CollisionResponse vtable slot (0x0017280c for Bomb,
    // Fruit equivalent). Called when the blade collision sphere hits this
    // entity. Default: no-op.
    virtual void OnSliced(const Vec3&) {}

    // Binary test: `(flags & 0x11) == 0`. Inactive / killed entities fail.
    bool IsActive() const { return (flags & ENT_SKIP_MASK) == 0; }
};

#endif
