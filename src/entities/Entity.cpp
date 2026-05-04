// Analysed: 2026-04-30T00:00
// Mortar::Entity base class and static method stubs.

#include "entities/Entity.h"
#include <cstdio>

// Binary @ 0x0019d708 / 0x0019d6d0: process-global LinkedHeap arena + size fields.
// Port: skip real arena (desktop libc handles fragmentation); keep sentinel lifecycle
// so operator new overrides and leak-detection hooks can anchor to it cleanly.
static void*         s_pEntityHeap    = nullptr;
static unsigned int  s_EntityHeapSize = 0;

// Binary @ 0x0019d88c — base ctor
Entity::Entity() : flags(0), m_RecycleFlag(0), entityType(0), m_Col(nullptr) {}

// Binary @ 0x0019d5cc — D1: restores vptr only, does NOT call Release
// Binary @ 0x0019d794 — D0: deleting variant (compiler-generated)
Entity::~Entity() {}

// Binary: Mortar::Entity::HeapCreate(size_t bytes) @ 0x0019d708.
// Called from GameInit step 15 with 0x20000 (128 KB).
// DIFFERS: original allocates a real LinkedHeap arena; port uses a non-null sentinel.
void Entity::HeapCreate(unsigned int size) {
    s_pEntityHeap    = reinterpret_cast<void*>(0x1);  // non-null sentinel
    s_EntityHeapSize = size;
}

// Binary: Mortar::Entity::HeapDestroy @ 0x0019d6d0.
// Called from GameExit to free the LinkedHeap arena.
// DIFFERS: original deletes the LinkedHeap; port zeroes the sentinel.
void Entity::HeapDestroy() {
    s_pEntityHeap    = nullptr;
    s_EntityHeapSize = 0;
}

// Binary @ 0x0019d5fc — base Init: no-op
void Entity::Init(int, int, int) {}

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

// Binary @ 0x0019d800 — base InRect: sphere-broadcast helper (no-op in base)
void Entity::InRect(float, float, float, float) {}

// Binary @ 0x0019d604 — base CollisionResponse: returns 0 (no-op)
void Entity::CollisionResponse(const Vec3&) {}

// Binary @ 0x0019d608 — slot 10: if m_Col and col, dispatch m_Col->Collide(col, hitPos)
void Entity::Collide(Entity* /*other*/, Mortar::Col* col, unsigned long* /*outFlags*/, Vec3* hitPos) {
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

// TODO: implement ET_RemoveEntity (binary @ 0x00174684) when EntityTracker tree storage is ported
void ET_RemoveEntity(int /*treeIdx*/, uint16_t /*trackerID*/) {}
