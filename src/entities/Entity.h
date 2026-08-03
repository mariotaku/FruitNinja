#ifndef FN_ENTITY_H
#define FN_ENTITY_H

#include "math/_Vector3.h"
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
// Layout verified from v1.6.1 Mortar::Entity::Entity @0x00256370:
//   CpuFill8(this, 0, 0x3c) -> ZERO_VEC3 into +0x10 and +0x1c -> strb #0,[+0x34]
//   -> str #0,[+0x38] -> bfi clearing bit5 of +0x0c. Scale (+0x28) is NOT zeroed
//   separately; the 0x3c fill covers it. sizeof(Entity) = 0x3C confirmed.
//
// Field map:
//   +0x00: vtable (4B)
//   +0x04: m_RuntimeID / uint32_t (4B) — RuntimeID / LoadEntity ID; matched by
//          ActorManager::FindByID against a trackerKey, and as the message senderId
//   +0x08: m_TrackerID / uint16_t (2B) — EntityTracker spatial-tree key
//   +0x0a: (2B gap)
//   +0x0c: flags / uint8_t (1B)
//   +0x0d: (3B gap)
//   +0x10: pos / Vec3 (12B)
//   +0x1c: vel / Vec3 (12B)
//   +0x28: scale / Vec3 (12B)
//   +0x34: m_RecycleFlag / uint8_t (1B)
//   +0x35: type / uint8_t (1B) — entity type
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

// ASM-spec v1.6.1 Mortar::Entity::Entity @ 0x00256370 -- see Entity.cpp for the
// ctor's field-by-field spec.
// UNVERIFIED: this carried an ASM-verified stamp the diff does not support. The
// binary bulk-fills 0x3C bytes and copies a zero Vector3 from a GOT global; the
// port initialises each member individually, so the padding at +0x0a/+0x0d is
// zeroed by the binary and left alone by the port. Every named field ends up
// with the same value, so no divergence is proven -- but this is the base ctor
// for every entity, so it wants a real asm-inspector pass, not a carried stamp.
class Entity {
public:
    // +0x04: RuntimeID / loader field. Set by LoadEntity; matched by
    // ActorManager::FindByID (trackerKey) and used as the message senderId.
    uint32_t m_RuntimeID;

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
    _Vector3<float> pos;

    // +0x1c..+0x27: velocity
    _Vector3<float> vel;

    // +0x28..+0x33: scale (visual size). Base ctor zeroes this; subclasses that
    // need scale=1 (Fruit, Bomb, BombBlast, Coin) must set it themselves.
    _Vector3<float> scale;

    // +0x34: recycle-state byte. Zeroed by ActorManager::Add on the
    // factory path (never on the recycle path). Port tracks it to stay
    // bit-faithful with the binary; no port code reads it yet.
    uint8_t m_RecycleFlag;

    // +0x35: entity type byte. Binary-faithful uint8_t on every target
    // (value range 0..6); uint8_t promotes to int in expressions/varargs
    // so gameplay code that indexes/compares with plain ints needs no cast.
    uint8_t entityType;

    // +0x36: 16-bit angle used by LoadEntity and Coin::Draw (Y-rotation index).
    // v1.6.1 Mortar::Entity::Entity @0x00256370: zeroed by the 0x3c fill.
    // BombBlast::Init writes random.
    uint16_t m_Angle;        // +0x36 in binary

    // +0x38: collision primitive pointer (nullable).
    Col* m_Col;     // +0x38 in binary -- polymorphic; subclasses install ColSphere/ColLine/ColAABB

    // v1.6.1 Mortar::Entity::Entity C1/C2 @0x00256370 — base ctor (both aliases
    // resolve to the same address in the symbol table)
    Entity();

    // v1.6.1 Mortar::Entity::~Entity D1/D2 @0x002561ec — restores vptr only, does NOT call Release
    // v1.6.1 Mortar::Entity::~Entity D0 @0x00256468 — deleting variant
    virtual ~Entity();

    // v1.6.1 Mortar::Entity::HeapCreate(size_t) @0x002564b0.
    // Called from GameInit step 15 with 0x20000 (128 KB) to allocate the
    // process-global LinkedHeap Entity arena before ActorManager::Initialise.
    // DIFFERS: original = LinkedHeap arena 0x20000, port uses std new (no fixed cap).
    static void HeapCreate(unsigned long bytes);

    // Counterpart to HeapCreate; called from GameExit.
    // v1.6.1 Mortar::Entity::HeapDestroy @0x00256508.
    static void HeapDestroy();

    // v1.6.1 Mortar::Entity::Activate @0x001d45f8 — clear bit0 (ENT_INACTIVE). Called by ActorManager::Add
    // recycle path. Single instruction: strb r0,[r0,#0x0c] where r0=flags & ~1.
    void Activate() { flags &= ~static_cast<uint8_t>(0x01u); }

    // Vtable slot 2 (+0x08): Init — v1.6.1 Mortar::Entity::Init @0x0025623c (base no-op).
    // Caller protocol: pos/vel pre-set, scale lives in p3 (nullable, default 1.0).
    // Bomb / Fruit / SlashEntity / BombBlast override; Coin uses base (no-op).
    // p1 and p2 are vestigial from the binary serialiser path; runtime callers
    // always pass (nullptr, 0, &scale).
    virtual void Init(void* /*payload, unused at runtime*/,
                      long   /*entityTypeOrLen, ignored except by .lvl loader*/,
                      _Vector3<float>* /*scaleOrNull; defaults to (1,1,1)*/);

    // Vtable slot 3 (+0x0C): Release — v1.6.1 Mortar::Entity::Release @0x00256210
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

    // Vtable slot 7 (+0x1C): PostLoad — v1.6.1 Mortar::Entity::PostLoad @0x00256240 (base no-op)
    virtual void PostLoad();

    // Vtable slot 8 (+0x20): InRect — v1.6.1 Mortar::Entity::InRect @0x002562a0
    // Signature: void InRect(ColAABB*) — sphere-broadcast helper.
    // Body reads aabb->_field_0x38 (inner Col*), copies pos fields, dispatches.
    // Called by ActorManager::GetNumInAABB. Port: no-op in base (body is complex
    // internal Col dispatch; callers in port use CollideWithSphere directly).
    virtual void InRect(ColAABB* aabb);

    // Vtable slot 9 (+0x24): CollisionResponse — v1.6.1 Mortar::Entity::CollisionResponse @0x00256244 (base returns 0)
    // Called when the blade collision sphere hits this entity.
    // Args 2/3 are always 0 at runtime (.lvl-loader vestige); kept in signature
    // for vtable parity. Returns int (Fruit: 1=already sliced, 0=ok; Bomb: 0;
    // base: 0).
    virtual int CollisionResponse(Entity* hitter,
                                  unsigned long /*flagsA*/,
                                  unsigned long /*flagsB*/,
                                  _Vector3<float>* bladeVelocity);

    // Vtable slot 10 (+0x28): Collide — v1.6.1 Mortar::Entity::Collide @0x0025624c
    // If m_Col, dispatch m_Col->Collide(col, hitPos)
    virtual void Collide(Entity* other, Col* col, unsigned long* outFlags, _Vector3<float>* hitPos);

    // Vtable slot 11 (+0x2C): ReceiveMessage — v1.6.1 Mortar::Entity::ReceiveMessage @0x00256274
    // msg->type 0 -> clear INACTIVE; type 1 -> set INACTIVE
    virtual void ReceiveMessage(Entity* sender, Mortar::Message* msg);

    // Vtable slot 12 (+0x30): ListenerCallback — v1.6.1 Mortar::Entity::ListenerCallback @0x001d7738.
    // Binary body is a single `bx lr`, i.e. it returns r0 == `this` (NOT the first
    // explicit argument). Vtable-only in the binary: 8 DATA xrefs, zero direct calls.
    virtual Entity* ListenerCallback(Entity* a, Entity* b, Mortar::Message* msg);

    // Binary test: `(flags & 0x11) == 0`. Inactive / killed entities fail.
    bool IsActive() const { return (flags & ENT_SKIP_MASK) == 0; }

    // Entity LinkedHeap allocation overrides.
    // v1.6.1 Mortar::Entity::operator new @0x002563e4 / operator new[] @0x00256414 /
    //   operator delete @0x0025643c / operator delete[] @0x00256484.
    // Both delete forms tail-call LinkedHeap::Release @0x00241640.
    // Route through the global LinkedHeap arena; fall back to global ::operator new/delete.
    static void* operator new(size_t size);
    static void  operator delete(void* p);
    static void* operator new[](size_t size);
    static void  operator delete[](void* p);

    // v1.6.1 Mortar::Entity::operator new(size_t, void*) @0x0025640c — placement new.
    // Binary body is `cpy r0,r1; bx lr`: returns the supplied storage unchanged.
    // Declared in-class by the binary, so it hides the global placement form for
    // Entity and its subclasses; behaviour is identical.
    static void* operator new(size_t size, void* place);

    // Entity LinkedHeap arena accessors (counterparts to HeapCreate/HeapDestroy).
    // Operate on the process-global LinkedHeap that HeapCreate allocates.
    // v1.6.1 Mortar::Entity::HeapClear @0x00256560 — tail-calls LinkedHeap::ReleaseAll @0x0010a138
    static void HeapClear();
    // v1.6.1 Mortar::Entity::HeapDisplay @0x00256580 — tail-calls LinkedHeap::DisplayUsage @0x002417a8 (bool = verbose)
    static void HeapDisplay(bool verbose);
    // v1.6.1 Mortar::Entity::HeapExist @0x002565e4 — report whether the global Entity LinkedHeap is allocated
    static bool HeapExist();
    // v1.6.1 Mortar::Entity::HeapGetFree @0x002565c4 — tail-calls LinkedHeap::GetTotalFreeMemory @0x0024122c
    static unsigned int HeapGetFree();
    // v1.6.1 Mortar::Entity::HeapGetSize @0x002565a4 — returns the s_EntityHeapSize global directly
    static unsigned int HeapGetSize();
};

#ifdef __bada__
// Binary-faithful layout asserts (cross-build only). entityType is uint8_t
// everywhere now, matching the binary bit-for-bit.
static_assert(offsetof(Entity, m_RuntimeID)  == 0x04, "m_RuntimeID offset wrong");
static_assert(offsetof(Entity, m_TrackerID)  == 0x08, "m_TrackerID offset wrong");
static_assert(offsetof(Entity, flags)        == 0x0C, "flags offset wrong");
static_assert(offsetof(Entity, pos)          == 0x10, "pos offset wrong");
static_assert(offsetof(Entity, vel)          == 0x1C, "vel offset wrong");
static_assert(offsetof(Entity, scale)        == 0x28, "scale offset wrong");
static_assert(offsetof(Entity, m_RecycleFlag)== 0x34, "m_RecycleFlag offset wrong");
static_assert(offsetof(Entity, entityType)   == 0x35, "entityType offset wrong");
static_assert(offsetof(Entity, m_Angle)      == 0x36, "m_Angle offset wrong");
static_assert(offsetof(Entity, m_Col)        == 0x38, "m_Col offset wrong");
static_assert(sizeof(Entity)                 == 0x3C, "sizeof(Entity) wrong");
#elif !defined(__EMSCRIPTEN__) && (defined(_WIN64) || defined(__LP64__) || defined(_LP64) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__))
// Always-on port layout asserts (desktop x64 MSVC/MinGW). Offsets reflect:
// 8-byte vtable ptr + 8-byte Col* on 64-bit; entityType is uint8_t (binary-
// faithful) so the tail no longer has an int-widen gap, just 8-byte pointer
// alignment padding before m_Col.
static_assert(offsetof(Entity, m_RuntimeID)  == 0x08, "m_RuntimeID port offset drift");
static_assert(offsetof(Entity, m_TrackerID)  == 0x0C, "m_TrackerID port offset drift");
static_assert(offsetof(Entity, flags)        == 0x10, "flags port offset drift");
static_assert(offsetof(Entity, pos)          == 0x14, "pos port offset drift");
static_assert(offsetof(Entity, vel)          == 0x20, "vel port offset drift");
static_assert(offsetof(Entity, scale)        == 0x2C, "scale port offset drift");
static_assert(offsetof(Entity, m_RecycleFlag)== 0x38, "m_RecycleFlag port offset drift");
static_assert(offsetof(Entity, entityType)   == 0x39, "entityType port offset drift (binary +0x35)");
static_assert(offsetof(Entity, m_Angle)      == 0x3A, "m_Angle port offset drift (binary +0x36)");
static_assert(offsetof(Entity, m_Col)        == 0x40, "m_Col port offset drift (binary +0x38)");
static_assert(sizeof(Entity)                 == 0x48, "sizeof(Entity) port drift (binary 0x3C)");
#endif

}  // namespace Mortar

// EntityTracker free functions (global namespace, matching binary symbols).
// Backing state: s_entityMap[3] (std::map<uint16_t, Entity*>) + s_currentIdent (uint16_t)
// in Entity.cpp. Trees 0/1/2 = P2P player-index partition; Fruit uses tree 0.

// ASM-spec v1.6.1 ET_GetFreeIdent @0x1d95bc
// Allocates a free 16-bit tracker ID in treeIdx. Single-probe, not a loop.
uint16_t ET_GetFreeIdent(int treeIdx);

// ASM-spec v1.6.1 ET_GetEntity @0x1d966c
// Returns the Entity* registered under `id` in treeIdx, or null if absent.
Mortar::Entity* ET_GetEntity(int treeIdx, uint16_t id);

// ASM-spec v1.6.1 ET_RemoveEntity @0x1d976c
// Removes the entry for `trackerID` from treeIdx (no-op if absent).
void ET_RemoveEntity(int treeIdx, uint16_t trackerID);

#endif
