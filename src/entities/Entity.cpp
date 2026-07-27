// Entity base class, static methods, and EntityTracker free functions.

#include "entities/Entity.h"
#include "util/LinkedHeap.h"
#include <cstdio>
#include <map>
#include <new>

// Binary @ 0x0019d708 / 0x0019d6d0: process-global LinkedHeap arena + size fields.
namespace Mortar {

static LinkedHeap*   s_pEntityHeap    = 0;
static unsigned int  s_EntityHeapSize = 0;

// Binary @ 0x0019d88c — base ctor.
// memset(this, 0, 0x3C) then explicit assignments. Scale stays 0 from memset
// (subclasses must set scale if they need 1.0). flags bit5 (NO_DESTRUCT) cleared.
Entity::Entity()
    : m_RuntimeID(0)
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
// Placement-new a LinkedHeap into a raw operator new(0x24) block.
// Called from GameInit step 15 with 0x20000 (128 KB).
void Entity::HeapCreate(unsigned long size) {
    void* p = ::operator new(sizeof(LinkedHeap));
    s_pEntityHeap    = new(p) LinkedHeap(size);
    s_EntityHeapSize = size;
}

// Binary: Entity::HeapDestroy @ 0x0019d6d0.
// Explicit dtor call + operator delete, matching placement-new in HeapCreate.
void Entity::HeapDestroy() {
    if (s_pEntityHeap) {
        s_pEntityHeap->~LinkedHeap();
        ::operator delete(s_pEntityHeap);
        s_pEntityHeap    = 0;
        s_EntityHeapSize = 0;
    }
}

// v1.6.1 Mortar::Entity::Init @0x0025623c — base Init: no-op (vtable slot 2).
// Runtime callers pass (nullptr, 0, &scale). .lvl loader passes header data;
// FruitNinja never loads .lvl files so that path is dead.
void Entity::Init(void* /*p1*/, long /*p2*/, _Vector3<float>* /*p3*/) {}

// Binary @ 0x0019d5e8 — base Release: dtor m_Col via vtable[1] then null it.
// Col has a virtual dtor; delete dispatches to the correct subclass dtor.
void Entity::Release() {
    if (m_Col) {
        delete m_Col;
        m_Col = nullptr;
    }
}

// v1.6.1 Mortar::Entity::PostLoad @0x00256240 — base PostLoad: no-op
void Entity::PostLoad() {}

// v1.6.1 Mortar::Entity::InRect @0x002562a0 — base InRect: sphere-broadcast helper (no-op in port base).
// Body in binary dispatches via inner Col* inside aabb->_field_0x38; the full
// internals of ColAABB spatial search are not yet ported. Port callers use
// CollideWithSphere directly instead of this vtable path.
void Entity::InRect(ColAABB* /*aabb*/) {}

// v1.6.1 Mortar::Entity::CollisionResponse @0x00256244 — base CollisionResponse: returns 0 (no-op).
// Vtable slot 9. Args 2/3 always 0 at runtime (.lvl-loader vestige).
int Entity::CollisionResponse(Entity* /*hitter*/,
                               unsigned long /*flagsA*/,
                               unsigned long /*flagsB*/,
                               _Vector3<float>* /*bladeVelocity*/) {
    return 0;
}

// v1.6.1 Mortar::Entity::Collide @0x0025624c — slot 10: if m_Col and col, dispatch m_Col->Collide(col, hitPos)
void Entity::Collide(Entity* /*other*/, Col* col, unsigned long* /*outFlags*/, _Vector3<float>* hitPos) {
    if (m_Col && col) {
        m_Col->Collide(col, hitPos);
    }
}

// v1.6.1 Mortar::Entity::ReceiveMessage @0x00256274 — slot 11: msg->type 0 -> clear INACTIVE; type 1 -> set INACTIVE
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

// EntityTracker globals.
// Binary: s_entityMap[0] @0x3328c8, [1] @0x3328e0, [2] @0x3328f8 (stride 0x18 = sizeof std::map).
// s_currentIdent @0x2d8d34 (uint16_t, shared across all 3 trees, monotonic).
// Trees 0/1/2 = P2P player-index partition; Fruit uses tree 0.
// Array length 3 confirmed by global ctor @0x1d9a40 (constructs 3) and __tcf_0 @0x1d96d4 (destroys 3).
static std::map<uint16_t, Mortar::Entity*> s_entityMap[3];
static uint16_t s_currentIdent = 0;

// ASM-spec v1.6.1 ET_GetFreeIdent @0x1d95bc: single-probe + single-bump (NOT a loop).
uint16_t ET_GetFreeIdent(int treeIdx) {
    if (s_currentIdent == 0) s_currentIdent = 1;
    std::map<uint16_t, Mortar::Entity*>& m = s_entityMap[treeIdx];
    if (m.find(s_currentIdent) != m.end()) {
        s_currentIdent++;
        if (s_currentIdent == 0) s_currentIdent = 1;
        // DIFFERS: binary v1.6.1 ET_GetFreeIdent @0x1d95bc re-probes find() here and discards result; port drops the dead call.
    }
    return s_currentIdent;
}

// ASM-spec v1.6.1 ET_GetEntity @0x1d966c: map lookup by 16-bit id, returns Entity* or null.
Mortar::Entity* ET_GetEntity(int treeIdx, uint16_t id) {
    std::map<uint16_t, Mortar::Entity*>& m = s_entityMap[treeIdx];
    std::map<uint16_t, Mortar::Entity*>::iterator it = m.find(id);
    if (it != m.end()) return it->second;
    return 0;
}

// ASM-spec v1.6.1 ET_RemoveEntity @0x1d976c: erase by id if present.
void ET_RemoveEntity(int treeIdx, uint16_t id) {
    std::map<uint16_t, Mortar::Entity*>& m = s_entityMap[treeIdx];
    std::map<uint16_t, Mortar::Entity*>::iterator it = m.find(id);
    if (it != m.end()) m.erase(it);
}

// Entity LinkedHeap arena accessors. The binary keeps two process-global slots,
// written by HeapCreate @ 0x0019d708: a LinkedHeap* (s_pEntityHeap) and the arena
// byte-size (s_EntityHeapSize).
namespace Mortar {

// Binary @ 0x0019d640 — return s_EntityHeapSize directly (NOT a LinkedHeap call).
unsigned int Entity::HeapGetSize() {
    return s_EntityHeapSize;
}

// Binary @ 0x0019d658 — true when the LinkedHeap pointer slot is populated.
bool Entity::HeapExist() {
    return s_pEntityHeap != 0;
}

// Binary @ 0x0019d678 — LinkedHeap::GetTotalFreeMemory on the global arena.
unsigned int Entity::HeapGetFree() {
    if (!s_pEntityHeap) return 0;
    return s_pEntityHeap->GetTotalFreeMemory();
}

// Binary @ 0x0019d694 — LinkedHeap::DisplayUsage on the global arena.
void Entity::HeapDisplay(bool verbose) {
    if (s_pEntityHeap) {
        s_pEntityHeap->DisplayUsage(verbose);
    }
}

// Binary @ 0x0019d6b4 — LinkedHeap::ReleaseAll on the global arena.
void Entity::HeapClear() {
    if (s_pEntityHeap) {
        s_pEntityHeap->ReleaseAll();
    }
}

// Binary @ 0x0019d7dc — Entity::operator new -> s_pEntityHeap->Allocate(size, NULL)
void* Entity::operator new(size_t size) {
    if (s_pEntityHeap) {
        void* p = s_pEntityHeap->Allocate((unsigned int)size, 0);
        if (p) return p;
    }
    return ::operator new(size);
}

// Binary @ 0x0019d770 — Entity::operator delete -> s_pEntityHeap->Release(p, false)
// Port-specific guard: if operator new fell back to ::operator new (entity heap full),
// the returned pointer is a system-heap address. Calling LinkedHeap::Release on it
// would interpret garbage memory as a Block header and corrupt the heap. Check
// Contains() first; if the pointer is outside the entity heap buffer, route to
// ::operator delete instead.
void Entity::operator delete(void* p) {
    if (p && s_pEntityHeap) {
        if (s_pEntityHeap->Contains(p)) {
            s_pEntityHeap->Release(p, false);
            return;
        }
    }
    ::operator delete(p);
}

// Binary @ 0x0019d7b8 — Entity::operator new[] -> s_pEntityHeap->Allocate(size, NULL)
void* Entity::operator new[](size_t size) {
    if (s_pEntityHeap) {
        void* p = s_pEntityHeap->Allocate((unsigned int)size, 0);
        if (p) return p;
    }
    return ::operator new[](size);
}

// Binary @ 0x0019d74c — Entity::operator delete[] -> s_pEntityHeap->Release(p, false)
// Same Contains() guard as operator delete (scalar) — see comment there.
void Entity::operator delete[](void* p) {
    if (p && s_pEntityHeap) {
        if (s_pEntityHeap->Contains(p)) {
            s_pEntityHeap->Release(p, false);
            return;
        }
    }
    ::operator delete[](p);
}

}  // namespace Mortar
