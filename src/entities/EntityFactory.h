#ifndef FN_ENTITY_FACTORY_H
#define FN_ENTITY_FACTORY_H

// EntityFactory — matches the free function `CreateEntity` at
// 0x0017421c in the binary. Registered with ActorManager as the
// Delegate1<Entity*, long> factory at struct offset +0x1024 (via
// ActorManager::RegisterFactory at 0x0016d870).
//
// See docs/functions/entity-factory-combo-timekeeper.md for the full
// decompile. Each type maps to a new-expression; unknown types return
// nullptr (matches the binary's default switch branch).
//
// Analysed: 2026-04-23T01:30

class Entity;

// 0x0017421c.
Entity* CreateEntity(int entityType);

#endif
