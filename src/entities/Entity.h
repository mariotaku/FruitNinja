#ifndef FN_ENTITY_H
#define FN_ENTITY_H

#include "math/Vec3.h"
#include "collision/Col.h"
#include "collision/ColSphere.h"
#include "collision/ColAABB.h"
#include "entities/Message.h"
#include <cstdint>

struct Renderer;

// Entity base class (0x3C bytes in binary, verified from ctor memset).
//
// Flag byte lives at Entity+0x0c. The binary encodes active / scheduled-
// for-deactivation / in-flight-update state in these bits — there is no
// separate `active` boolean. Keeping the port in sync with the bit layout
// is required for ActorManager::Update and Draw to gate iteration the
// same way the binary does (test `(flags & 0x11) == 0`).
//
// Analysed: 2026-05-04T00:00
//
// Layout verified from ctor at 0x0019d88c (memset 0x3C bytes):
//   +0x00: vtable (4B)
//   +0x04: field_0x04 / uint32_t (4B) — RuntimeID or LoadEntity ID
//   +0x08: m_TrackerID / uint16_t (2B) — EntityTracker spatial-tree key
//   +0x0a: (2B gap)
//   +0x0c: flags / uint8_t (1B)
//   +0x0d: (3B gap)
//   +0x10: pos / Vec3 (12B)
//   +0x1c: vel / Vec3 (12B)
//   +0x28: scale / Vec3 (12B)
//   +0x34: m_RecycleFlag / uint8_t (1B)
//   +0x35: type / uint8_t (1B) — entity type; port widens to int
//   +0x36: m_Angle / uint16_t (2B) — used by LoadEntity and Coin::Draw
//   +0x38: m_Col / Col* (4B)
//   sizeof = 0x3C (60)
enum EntityFlagBits : uint8_t {
    ENT_INACTIVE        = 0x01,  // cleared by Entity::Activate on pool recycle
    ENT_HAS_COLLISION   = 0x02,  // set by Init overrides that install m_Col
                                 // (Fruit, Bomb, SlashEntity); never tested at runtime
    ENT_UPDATED_HALF1   = 0x04,  // set with 0x08 by ActorManager::Update BEFORE
                                 // vtable Update/PostUpdate dispatch (orr #0xc).
                                 // Never cleared, never read -- advisory only.
    ENT_UPDATED_HALF2   = 0x08,  // pair with 0x04; binary always sets them together
    ENT_KILLED          = 0x10,  // entity wants to retire; swept by Update
    ENT_NO_DESTRUCT     = 0x20,  // ActorManager::Remove/::Clear skip vtable Release if set
    ENT_TICK_DISPATCHED = ENT_UPDATED_HALF1 | ENT_UPDATED_HALF2,  // 0x0c -- write together
    // 0x11 is the combined "skip" mask -- an entity is processed only when
    // `(flags & 0x11) == 0`.
    ENT_SKIP_MASK       = ENT_INACTIVE | ENT_KILLED,
};

namespace Mortar {

// ASM-verified: 2026-04-28T15:55Z binary @ 0x0019d88c (asm-inspector)
// ASM-verified: 2026-04-28T15:55Z binary @ 0x001ea478 (asm-inspector)
class Entity {
public:
    // +0x04: RuntimeID / loader field. Set by LoadEntity; unread at runtime.
    uint32_t field_0x04;

    // +0x08: EntityTracker spatial-tree key. Assigned on spawn registration.
    // Binary @ Fruit::Init sets this->m_TrackerID = 0.
    uint16_t m_TrackerID;    // +0x08

    // +0x0a: 2-byte gap. Explicit so binary layout (+0x0c flags) holds
    // under any compiler -- natural alignment after a uint16_t doesn't pad
    // before the next uint8_t.
    uint16_t pad_0x0a;       // +0x0a

    // +0x0c: flag byte (see EntityFlagBits above).
    uint8_t flags;

    // +0x0d: 3-byte gap. Compiler will naturally pad here for Vec3's
    // 4-byte alignment at +0x10, but documented for clarity. Entity::Entity
    // memset 0x3C zeroes the whole struct including these gap bytes.

    // +0x10..+0x1b: position
    Vec3 pos;

    // +0x1c..+0x27: velocity
    Vec3 vel;

    // +0x28..+0x33: scale (visual size). Base ctor zeroes this; subclasses that
    // need scale=1 (Fruit, Bomb, BombBlast, Coin) must set it themselves.
    Vec3 scale;

    // +0x34: recycle-state byte. Zeroed by ActorManager::Add on the
    // factory path (never on the recycle path). Port tracks it to stay
    // bit-faithful with the binary; no port code reads it yet.
    uint8_t m_RecycleFlag;

    // +0x35: entity type byte in the binary. Port widens to int since
    // gameplay code indexes with regular ints; value range is still 0..4.
    // DIFFERS: binary = uint8_t at +0x35; port = int (desktop). Under
    //   __bada__ it reverts to uint8_t so binary-faithful offsetof checks
    //   hold for the cross-build assertion block.
#ifdef __bada__
    uint8_t entityType;
#else
    int entityType;
#endif

    // +0x36: 16-bit angle used by LoadEntity and Coin::Draw (Y-rotation index).
    // Binary @ 0x0019d88c ctor: zeroed by memset. BombBlast::Init writes random.
    uint16_t m_Angle;        // +0x36 in binary; offset may differ in port due to entityType widening

    // +0x38: collision primitive pointer (nullable).
    Col* m_Col;     // +0x38 in binary -- polymorphic; subclasses install ColSphere/ColLine/ColAABB

    // Binary @ 0x0019d88c — base ctor
    Entity();

    // Binary @ 0x0019d5cc — D1: restores vptr only, does NOT call Release
    // Binary @ 0x0019d794 — D0: deleting variant
    virtual ~Entity();

    // Binary: Entity::HeapCreate(size_t) @ 0x0019d708 (40 bytes).
    // Called from GameInit step 15 with 0x20000 (128 KB) to allocate the
    // process-global LinkedHeap Entity arena before ActorManager::Initialise.
    // DIFFERS: original = LinkedHeap arena 0x20000, port uses std new (no fixed cap).
    static void HeapCreate(unsigned int bytes);

    // Counterpart to HeapCreate; called from GameExit.
    // Binary: Entity::HeapDestroy @ 0x0019d6d0.
    static void HeapDestroy();

    // Binary @ 0x00170b18 — clear bit0 (ENT_INACTIVE). Called by ActorManager::Add
    // recycle path. Single instruction: strb r0,[r0,#0x0c] where r0=flags & ~1.
    void Activate() { flags &= ~static_cast<uint8_t>(0x01u); }

    // Vtable slot 2 (+0x08): Init — Binary @ 0x0019d5fc (base no-op).
    // Caller protocol: pos/vel pre-set, scale lives in p3 (nullable, default 1.0).
    // Bomb / Fruit / SlashEntity / BombBlast override; Coin uses base (no-op).
    // p1 and p2 are vestigial from the binary serialiser path; runtime callers
    // always pass (nullptr, 0, &scale). Binary @ 0x0019d5fc.
    virtual void Init(void* /*payload, unused at runtime*/,
                      long   /*entityTypeOrLen, ignored except by .lvl loader*/,
                      Vec3* /*scaleOrNull; defaults to (1,1,1)*/);

    // Vtable slot 3 (+0x0C): Release — Binary @ 0x0019d5e8
    // Base: frees m_Col then nulls it. Subclasses override to release resources.
    virtual void Release();

    // Vtable slot 4 (+0x10): Update — PURE VIRTUAL (binary: __cxa_pure_virtual @ 0x002773d0)
    virtual void Update(float dt) = 0;

    // Vtable slot 5 (+0x14): Draw — PURE VIRTUAL (binary: __cxa_pure_virtual @ 0x002773d0)
    virtual void Draw(Renderer&) = 0;

    // Vtable slot 6 (+0x18): PostUpdate (binary name: DrawUpdate) — PURE VIRTUAL
    // Called from ActorManager::Update right after Update, still under
    // the gate `(flags & 0x11) == 0`.
    virtual void PostUpdate(float dt) = 0;

    // Vtable slot 7 (+0x1C): PostLoad — Binary @ 0x0019d600 (base no-op)
    virtual void PostLoad();

    // Vtable slot 8 (+0x20): InRect — Binary @ 0x0019d800
    // Signature: void InRect(ColAABB*) — sphere-broadcast helper.
    // Body reads aabb->_field_0x38 (inner Col*), copies pos fields, dispatches.
    // Called by ActorManager::GetNumInAABB. Port: no-op in base (body is complex
    // internal Col dispatch; callers in port use CollideWithSphere directly).
    // Binary @ 0x0019d800.
    virtual void InRect(ColAABB* aabb);

    // Vtable slot 9 (+0x24): CollisionResponse — Binary @ 0x0019d604 (base returns 0)
    // Called when the blade collision sphere hits this entity.
    // Args 2/3 are always 0 at runtime (.lvl-loader vestige); kept in signature
    // for vtable parity. Returns int (Fruit: 1=already sliced, 0=ok; Bomb: 0;
    // base: 0). Binary @ 0x0019d604.
    virtual int CollisionResponse(Entity* hitter,
                                  unsigned long /*flagsA*/,
                                  unsigned long /*flagsB*/,
                                  Vec3*  bladeVelocity);

    // Vtable slot 10 (+0x28): Collide — Binary @ 0x0019d608
    // If m_Col, dispatch m_Col->Collide(col, hitPos)
    virtual void Collide(Entity* other, Col* col, unsigned long* outFlags, Vec3* hitPos);

    // Vtable slot 11 (+0x2C): ReceiveMessage — Binary @ 0x0019d61c
    // msg->type 0 -> clear INACTIVE; type 1 -> set INACTIVE
    virtual void ReceiveMessage(Entity* sender, Mortar::Message* msg);

    // Vtable slot 12 (+0x30): ListenerCallback — Binary @ 0x00172f4c
    // Returns first argument (identity).
    virtual Entity* ListenerCallback(Entity* a, Entity* b, Mortar::Message* msg);

    // Binary test: `(flags & 0x11) == 0`. Inactive / killed entities fail.
    bool IsActive() const { return (flags & ENT_SKIP_MASK) == 0; }

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: Entity::HeapClear -- auto stub from binary missing-symbol set
    void HeapClear();
    // STUB: Entity::HeapDisplay -- auto stub from binary missing-symbol set
    void HeapDisplay(bool);
    // STUB: Entity::HeapExist -- auto stub from binary missing-symbol set
    void HeapExist();
    // STUB: Entity::HeapGetFree -- auto stub from binary missing-symbol set
    void HeapGetFree();
    // STUB: Entity::HeapGetSize -- auto stub from binary missing-symbol set
    void HeapGetSize();
    // ---- end AUTO-STUB MERGE ----
};

#ifdef __bada__
// Binary-faithful layout asserts (cross-build only). Under __bada__, entityType
// reverts to uint8_t matching the binary, so all post-m_RecycleFlag offsets hold.
static_assert(offsetof(Entity, field_0x04)   == 0x04, "field_0x04 offset wrong");
static_assert(offsetof(Entity, m_TrackerID)  == 0x08, "m_TrackerID offset wrong");
static_assert(offsetof(Entity, flags)        == 0x0C, "flags offset wrong");
static_assert(offsetof(Entity, pos)          == 0x10, "pos offset wrong");
static_assert(offsetof(Entity, vel)          == 0x1C, "vel offset wrong");
static_assert(offsetof(Entity, scale)        == 0x28, "scale offset wrong");
static_assert(offsetof(Entity, m_RecycleFlag)== 0x34, "m_RecycleFlag offset wrong");
static_assert(offsetof(Entity, entityType)   == 0x35, "entityType offset wrong (binary uint8_t)");
static_assert(offsetof(Entity, m_Angle)      == 0x36, "m_Angle offset wrong");
static_assert(offsetof(Entity, m_Col)        == 0x38, "m_Col offset wrong");
static_assert(sizeof(Entity)                 == 0x3C, "sizeof(Entity) wrong");
#else
// Always-on port layout asserts (desktop x64 MSVC/MinGW). Offsets reflect:
// 8-byte vtable ptr on 64-bit, int-widened entityType, Col* at 8-byte pointer size.
static_assert(offsetof(Entity, field_0x04)   == 0x08, "field_0x04 port offset drift");
static_assert(offsetof(Entity, m_TrackerID)  == 0x0C, "m_TrackerID port offset drift");
static_assert(offsetof(Entity, flags)        == 0x10, "flags port offset drift");
static_assert(offsetof(Entity, pos)          == 0x14, "pos port offset drift");
static_assert(offsetof(Entity, vel)          == 0x20, "vel port offset drift");
static_assert(offsetof(Entity, scale)        == 0x2C, "scale port offset drift");
static_assert(offsetof(Entity, m_RecycleFlag)== 0x38, "m_RecycleFlag port offset drift");
static_assert(offsetof(Entity, entityType)   == 0x3C, "entityType port offset drift (int, binary +0x35 uint8_t)");
static_assert(offsetof(Entity, m_Angle)      == 0x40, "m_Angle port offset drift (binary +0x36)");
static_assert(offsetof(Entity, m_Col)        == 0x48, "m_Col port offset drift (binary +0x38)");
static_assert(sizeof(Entity)                 == 0x50, "sizeof(Entity) port drift (binary 0x3C)");
#endif

}  // namespace Mortar

// Free function: remove an entity from EntityTracker tree `treeIdx` by its
// 16-bit tracker ID. Called by Fruit::KillFruit to unregister the dying
// fruit from the spatial acceleration structure.
// TODO: implement ET_RemoveEntity (binary @ 0x00174684) when EntityTracker tree storage is ported
void ET_RemoveEntity(int treeIdx, uint16_t trackerID);

#endif
