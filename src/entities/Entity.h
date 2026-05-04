#ifndef FN_ENTITY_H
#define FN_ENTITY_H

#include "math/Vec3.h"
#include "collision/ColSphere.h"
#include <cstdint>

struct Renderer;

// Mortar::Entity base class (0x3c bytes in binary, verified from ctor memset).
//
// Flag byte lives at Entity+0x0c. The binary encodes active / scheduled-
// for-deactivation / in-flight-update state in these bits — there is no
// separate `active` boolean. Keeping the port in sync with the bit layout
// is required for ActorManager::Update and Draw to gate iteration the
// same way the binary does (test `(flags & 0x11) == 0`).
//
// Analysed: 2026-04-29T00:00
//
// TODO: Entity RE deviations deferred (cascade across all subclasses):
//  - Update / Draw / PostUpdate should be pure-virtual (=0). Currently empty
//    bodies. Binary uses __cxa_pure_virtual @ 0x002773d0 in slots 4/5/6.
//  - Init signature: binary is (void*, long, const Vec3*); port has (int, int, int).
//    Affects Bomb::Init / Fruit::Init / SplatEntity::Init signatures.
//  - CollisionResponse signature: binary is (Entity* hitter, u32, u32, const Vec3*);
//    port has (const Vec3&). Affects Bomb / Fruit / SlashEntity overrides.
//  - ~Entity D1 does NOT call Release; verify each subclass D1 dtor calls Release
//    explicitly before chaining (Bomb/Fruit/SlashEntity/Coin/SplatEntity).
//  - Vec3 scale stays 0 from base ctor; subclasses that need 1.0 must set it.
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
//
// ASM-verified: 2026-04-28T15:55Z binary @ 0x0019d88c (asm-inspector)
// ASM-verified: 2026-04-28T15:55Z binary @ 0x001ea478 (asm-inspector)
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

    // +0x38: collision sphere pointer (nullable). Binary ctor stores nullptr
    // here (verified: str r6,[r4,#0x38] where r6=0). Subclasses that need
    // collision allocate a ColSphere in Init (if null) and free in dtor.
    // BombBlast leaves this null (no collision).
    Mortar::ColSphere* m_Col;

    // Binary @ 0x0019d88c — base ctor
    Entity();

    // Binary @ 0x0019d5cc — D1: restores vptr only, does NOT call Release
    // Binary @ 0x0019d794 — D0: deleting variant
    virtual ~Entity();

    // Binary: Mortar::Entity::HeapCreate(size_t) @ 0x0019d708 (40 bytes).
    // Called from GameInit step 15 with 0x20000 (128 KB) to allocate the
    // process-global LinkedHeap Entity arena before ActorManager::Initialise.
    // DIFFERS: original = LinkedHeap arena 0x20000, port uses std new (no fixed cap).
    // TODO: implement -- see docs/systems/gameinit-todos.md step 15.
    static void HeapCreate(unsigned int bytes);

    // Counterpart to HeapCreate; called from GameExit.
    // Binary: Mortar::Entity::HeapDestroy @ 0x0019d6d0.
    // TODO: implement -- see docs/systems/gameinit-todos.md step 15.
    static void HeapDestroy();

    // Vtable slot 2 (+0x08): Init — Binary @ 0x0019d5fc (base no-op)
    virtual void Init(int, int, int);

    // Vtable slot 3 (+0x0C): Release — Binary @ 0x0019d5e8
    // Base: frees m_Col then nulls it. Subclasses override to release resources.
    virtual void Release();

    // Vtable slot 4 (+0x10): Update
    virtual void Update(float) {}

    // Vtable slot 5 (+0x14): Draw
    virtual void Draw(Renderer&) {}

    // Vtable slot 6 (+0x18): PostUpdate (binary name: DrawUpdate).
    // Called from ActorManager::Update right after Update, still under
    // the gate `(flags & 0x11) == 0`. Bomb uses it to sync its fuse-emitter
    // position with the rotation that Update just advanced.
    virtual void PostUpdate(float) {}

    // Vtable slot 7 (+0x1C): PostLoad — Binary @ 0x0019d600 (base no-op)
    virtual void PostLoad();

    // Vtable slot 8 (+0x20): InRect — Binary @ 0x0019d800 (sphere-broadcast helper)
    // Base no-op; subclasses update m_Col->center from pos and dispatch collision.
    virtual void InRect(float, float, float, float);

    // Vtable slot 9 (+0x24): CollisionResponse — Binary @ 0x0019d604 (base returns 0)
    // Called when the blade collision sphere hits this entity.
    // Port previously named this OnSliced; renamed to match binary symbol.
    virtual void CollisionResponse(const Vec3&);

    // Vtable slot 10 (+0x28): Collide — Binary @ 0x0019d608
    // If m_Col, dispatch m_Col->vtable[3] with (col, hitPos, outFlags)
    virtual void Collide(Entity* other, void* col, unsigned long* outFlags, Vec3* hitPos);

    // Vtable slot 11 (+0x2C): ReceiveMessage — Binary @ 0x0019d61c
    // msg.type 0 -> clear INACTIVE; type 1 -> set INACTIVE
    virtual void ReceiveMessage(Entity* sender, void* msg);

    // Vtable slot 12 (+0x30): ListenerCallback — Binary @ 0x00172f4c
    // No-op: returns first argument (identity-return).
    virtual Entity* ListenerCallback(Entity* a, Entity* b, void* msg);

    // Binary test: `(flags & 0x11) == 0`. Inactive / killed entities fail.
    bool IsActive() const { return (flags & ENT_SKIP_MASK) == 0; }
};

// Free function: remove an entity from EntityTracker tree `treeIdx` by its
// 16-bit tracker ID. Called by Fruit::KillFruit to unregister the dying
// fruit from the spatial acceleration structure.
// TODO: implement ET_RemoveEntity (binary @ 0x00174684) when EntityTracker tree storage is ported
void ET_RemoveEntity(int treeIdx, uint16_t trackerID);

#endif
