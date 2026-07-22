#ifndef FN_ENTITY_FACTORY_H
#define FN_ENTITY_FACTORY_H

// EntityFactory — matches the free function `CreateEntity` at
// 0x0017421c in the binary. Registered with Mortar::ActorManager as the
// Mortar::Delegate1<Mortar::Entity*, long> factory at struct offset +0x1024 (via
// Mortar::ActorManager::RegisterFactory at 0x0016d870).
//
// Each type maps to a new-expression; unknown types return
// nullptr (matches the binary's default switch branch).
//
// Analysed: 2026-04-23T01:30

namespace Mortar { class Entity; }

// Binary: _Z12CreateEntityl @0x001d8fec (v1.6.1)
Mortar::Entity* CreateEntity(long entityType);

#endif
