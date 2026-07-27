// Entity base class, static methods, and EntityTracker free functions.

#include "entities/Entity.h"
#include "util/LinkedHeap.h"
#include <cstdio>
#include <map>
#include <new>

// v1.6.1 Mortar::Entity::HeapCreate @0x002564b0 / HeapDestroy @0x00256508:
// process-global LinkedHeap arena + size fields. The binary keeps these as static
// class members Mortar::Entity::m_heap @0x0035cdfc and m_heap_size @0x0035ce00.
namespace Mortar {

static LinkedHeap*   s_pEntityHeap    = 0;
static unsigned int  s_EntityHeapSize = 0;

// v1.6.1 Mortar::Entity::Entity C1/C2 @0x00256370 — base ctor.
// CpuFill8(this, 0, 0x3C) then explicit assignments. Scale (+0x28) stays 0 from the
// fill (subclasses must set scale if they need 1.0). flags bit5 (NO_DESTRUCT) cleared
// via bfi on +0x0c.
Entity::Entity()
    : m_RuntimeID(0)
    , m_TrackerID(0)
    , flags(0)
    , m_RecycleFlag(0)
    , entityType(0)
    , m_Angle(0)
    , m_Col(nullptr)
{
    // Binary: after the fill, pos = ZERO_VEC3, vel = ZERO_VEC3, m_RecycleFlag = 0,
    // m_Col = nullptr, flags &= ~0x20 (bit5 NO_DESTRUCT off). Memberwise init above
    // covers this.
}

// v1.6.1 Mortar::Entity::~Entity D1/D2 @0x002561ec — restores vptr only, does NOT call Release
// v1.6.1 Mortar::Entity::~Entity D0 @0x00256468 — deleting variant (compiler-generated)
Entity::~Entity() {}

// v1.6.1 Mortar::Entity::HeapCreate(size_t bytes) @0x002564b0.
// Placement-new a LinkedHeap into a raw operator new(0x24) block.
// Called from GameInit step 15 with 0x20000 (128 KB).
void Entity::HeapCreate(unsigned long size) {
    void* p = ::operator new(sizeof(LinkedHeap));
    s_pEntityHeap    = new(p) LinkedHeap(size);
    s_EntityHeapSize = size;
}

// v1.6.1 Mortar::Entity::HeapDestroy @0x00256508.
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

// v1.6.1 Mortar::Entity::Release @0x00256210 — base Release: dtor m_Col via vtable[1] then null it.
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

// v1.6.1 Mortar::Entity::ListenerCallback @0x001d7738 — slot 12. The whole body is a
// single `bx lr`, so r0 is left holding the `this` pointer: the binary returns `this`,
// not the first explicit argument. Vtable-only (8 DATA xrefs, no direct calls).
Entity* Entity::ListenerCallback(Entity* /*a*/, Entity* /*b*/, Mortar::Message* /*msg*/) {
    return this;
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
// written by HeapCreate @0x002564b0: a LinkedHeap* (m_heap @0x0035cdfc) and the arena
// byte-size (m_heap_size @0x0035ce00).
namespace Mortar {

// v1.6.1 Mortar::Entity::HeapGetSize @0x002565a4 — return s_EntityHeapSize directly (NOT a LinkedHeap call).
unsigned int Entity::HeapGetSize() {
    return s_EntityHeapSize;
}

// v1.6.1 Mortar::Entity::HeapExist @0x002565e4 — true when the LinkedHeap pointer slot is populated.
bool Entity::HeapExist() {
    return s_pEntityHeap != 0;
}

// v1.6.1 Mortar::Entity::HeapGetFree @0x002565c4 — tail-calls LinkedHeap::GetTotalFreeMemory @0x0024122c.
unsigned int Entity::HeapGetFree() {
    if (!s_pEntityHeap) return 0;
    return s_pEntityHeap->GetTotalFreeMemory();
}

// v1.6.1 Mortar::Entity::HeapDisplay @0x00256580 — tail-calls LinkedHeap::DisplayUsage @0x002417a8.
void Entity::HeapDisplay(bool verbose) {
    if (s_pEntityHeap) {
        s_pEntityHeap->DisplayUsage(verbose);
    }
}

// v1.6.1 Mortar::Entity::HeapClear @0x00256560 — tail-calls LinkedHeap::ReleaseAll @0x0010a138.
void Entity::HeapClear() {
    if (s_pEntityHeap) {
        s_pEntityHeap->ReleaseAll();
    }
}

// v1.6.1 Mortar::Entity::operator new @0x002563e4 -> s_pEntityHeap->Allocate(size, NULL)
void* Entity::operator new(size_t size) {
    if (s_pEntityHeap) {
        void* p = s_pEntityHeap->Allocate((unsigned int)size, 0);
        if (p) return p;
    }
    return ::operator new(size);
}

// v1.6.1 Mortar::Entity::operator delete @0x0025643c -> tail-calls LinkedHeap::Release @0x00241640
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

// v1.6.1 Mortar::Entity::operator new(size_t, void*) @0x0025640c — placement new.
// Binary body is `cpy r0,r1; bx lr`: the arena is bypassed entirely and the caller's
// storage is returned unchanged.
void* Entity::operator new(size_t /*size*/, void* place) {
    return place;
}

// v1.6.1 Mortar::Entity::operator new[] @0x00256414 -> s_pEntityHeap->Allocate(size, NULL)
void* Entity::operator new[](size_t size) {
    if (s_pEntityHeap) {
        void* p = s_pEntityHeap->Allocate((unsigned int)size, 0);
        if (p) return p;
    }
    return ::operator new[](size);
}

// v1.6.1 Mortar::Entity::operator delete[] @0x00256484 -> tail-calls LinkedHeap::Release @0x00241640
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
