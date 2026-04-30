// Analysed: 2026-04-30T00:00
// Mortar::Entity static method stubs.

#include "entities/Entity.h"
#include <cstdio>

// Binary @ 0x0019d708 / 0x0019d6d0: process-global LinkedHeap arena + size fields.
// Port: skip real arena (desktop libc handles fragmentation); keep sentinel lifecycle
// so operator new overrides and leak-detection hooks can anchor to it cleanly.
static void*         s_pEntityHeap    = nullptr;
static unsigned int  s_EntityHeapSize = 0;

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
