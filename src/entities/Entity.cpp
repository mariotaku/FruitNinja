// Analysed: 2026-05-04T00:00
// Entity base class and static method stubs.

#include "entities/Entity.h"
#include <cstdio>

// Binary @ 0x0019d708 / 0x0019d6d0: process-global LinkedHeap arena + size fields.
// Port: skip real arena (desktop libc handles fragmentation); keep sentinel lifecycle
// so operator new overrides and leak-detection hooks can anchor to it cleanly.
namespace Mortar {

static void*         s_pEntityHeap    = nullptr;
static unsigned int  s_EntityHeapSize = 0;

// Binary @ 0x0019d88c — base ctor.
// memset(this, 0, 0x3C) then explicit assignments. Scale stays 0 from memset
// (subclasses must set scale if they need 1.0). flags bit5 (NO_DESTRUCT) cleared.
Entity::Entity()
    : field_0x04(0)
    , m_TrackerID(0)
    , flags(0)
    , m_RecycleFlag(0)
    , entityType(0)
    , m_Angle(0)
    , m_Col(nullptr)
{
    // Binary: after memset, pos = ZERO_VEC3, vel = ZERO_VEC3, m_Col = nullptr,
    // flags &= ~0x20 (bit5 NO_DESTRUCT off). Memberwise init above covers this.
}

// Binary @ 0x0019d5cc — D1: restores vptr only, does NOT call Release
// Binary @ 0x0019d794 — D0: deleting variant (compiler-generated)
Entity::~Entity() {}

// Binary: Entity::HeapCreate(size_t bytes) @ 0x0019d708.
// Called from GameInit step 15 with 0x20000 (128 KB).
// DIFFERS: original allocates a real LinkedHeap arena; port uses a non-null sentinel.
void Entity::HeapCreate(unsigned int size) {
    s_pEntityHeap    = reinterpret_cast<void*>(0x1);  // non-null sentinel
    s_EntityHeapSize = size;
}

// Binary: Entity::HeapDestroy @ 0x0019d6d0.
// Called from GameExit to free the LinkedHeap arena.
// DIFFERS: original deletes the LinkedHeap; port zeroes the sentinel.
void Entity::HeapDestroy() {
    s_pEntityHeap    = nullptr;
    s_EntityHeapSize = 0;
}

// Binary @ 0x0019d5fc — base Init: no-op (vtable slot 2).
// Runtime callers pass (nullptr, 0, &scale). .lvl loader passes header data;
// FruitNinja never loads .lvl files so that path is dead.
void Entity::Init(void* /*p1*/, long /*p2*/, Vec3* /*p3*/) {}

// Binary @ 0x0019d5e8 — base Release: dtor m_Col via vtable[1] then null it.
// Col has a virtual dtor; delete dispatches to the correct subclass dtor.
void Entity::Release() {
    if (m_Col) {
        delete m_Col;
        m_Col = nullptr;
    }
}

// Binary @ 0x0019d600 — base PostLoad: no-op
void Entity::PostLoad() {}

// Binary @ 0x0019d800 — base InRect: sphere-broadcast helper (no-op in port base).
// Body in binary dispatches via inner Col* inside aabb->_field_0x38; the full
// internals of ColAABB spatial search are not yet ported. Port callers use
// CollideWithSphere directly instead of this vtable path.
void Entity::InRect(ColAABB* /*aabb*/) {}

// Binary @ 0x0019d604 — base CollisionResponse: returns 0 (no-op).
// Vtable slot 9. Args 2/3 always 0 at runtime (.lvl-loader vestige).
int Entity::CollisionResponse(Entity* /*hitter*/,
                               unsigned long /*flagsA*/,
                               unsigned long /*flagsB*/,
                               Vec3* /*bladeVelocity*/) {
    return 0;
}

// Binary @ 0x0019d608 — slot 10: if m_Col and col, dispatch m_Col->Collide(col, hitPos)
void Entity::Collide(Entity* /*other*/, Col* col, unsigned long* /*outFlags*/, Vec3* hitPos) {
    if (m_Col && col) {
        m_Col->Collide(col, hitPos);
    }
}

// Binary @ 0x0019d61c — slot 11: msg->type 0 -> clear INACTIVE; type 1 -> set INACTIVE
void Entity::ReceiveMessage(Entity* /*sender*/, Mortar::Message* msg) {
    if (!msg) return;
    int t = msg->type;
    if (t == 0)      flags &= ~0x01u;
    else if (t == 1) flags |=  0x01u;
}

// Binary @ 0x00172f4c — slot 12: ListenerCallback identity (returns first arg)
Entity* Entity::ListenerCallback(Entity* a, Entity* /*b*/, Mortar::Message* /*msg*/) {
    return a;
}

}  // namespace Mortar

// TODO: implement ET_RemoveEntity (binary @ 0x00174684) when EntityTracker tree storage is ported
void ET_RemoveEntity(int /*treeIdx*/, uint16_t /*trackerID*/) {}

// Entity LinkedHeap arena accessors. Bodies operate on the global LinkedHeap
// allocated by HeapCreate; the port's heap path uses std new (see HeapCreate
// DIFFERS), so these remain no-ops until the real LinkedHeap arena is ported.
namespace Mortar {
// TODO: 0x0019d6b4 -- HeapClear: LinkedHeap::ReleaseAll on the global Entity arena
void Entity::HeapClear() {}
// TODO: 0x0019d694 -- HeapDisplay: debug-dump the Entity LinkedHeap (bool = verbose)
void Entity::HeapDisplay(bool) {}
// TODO: 0x0019d658 -- HeapExist: report whether the global Entity LinkedHeap is allocated
void Entity::HeapExist() {}
// TODO: 0x0019d678 -- HeapGetFree: return free byte count of the Entity LinkedHeap
void Entity::HeapGetFree() {}
// TODO: 0x0019d640 -- HeapGetSize: return total byte size of the Entity LinkedHeap
void Entity::HeapGetSize() {}
}  // namespace Mortar
