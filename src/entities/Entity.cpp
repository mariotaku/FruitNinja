// Analysed: 2026-04-30T00:00
// Mortar::Entity static method stubs.

#include "entities/Entity.h"
#include <cstdio>

// Binary: Mortar::Entity::HeapCreate(size_t bytes) @ 0x0019d708 (40 bytes).
// Called from GameInit step 15 @ 0x0016cb48 with 0x20000 (128 KB).
// Allocates the process-global LinkedHeap arena used by ActorManager type-lists.
// DIFFERS: original = LinkedHeap arena 0x20000, port uses std new (no fixed cap).
// TODO: implement -- see docs/systems/gameinit-todos.md step 15.
void Entity::HeapCreate(unsigned int /*bytes*/) {
    // TODO: implement Entity::HeapCreate -- see docs/systems/gameinit-todos.md step 15.
}

// Binary: Mortar::Entity::HeapDestroy -- counterpart to HeapCreate.
// Called from GameExit to free the LinkedHeap arena.
// TODO: implement -- see docs/systems/gameinit-todos.md step 15.
void Entity::HeapDestroy() {
    // TODO: implement Entity::HeapDestroy -- see docs/systems/gameinit-todos.md step 15.
}
