#ifndef FN_ENTITIES_ENTITY_TRACKER_H
#define FN_ENTITIES_ENTITY_TRACKER_H

// Defunct: P2P EntityTracker -- no-op stubs; v1.6.1 @0x001c970c area.
//
// EntityTracker is the spatial key / entity registry used by the P2P multiplayer
// subsystem to synchronise entity state between peers. In v1.6.1 the Bada build
// has no live P2P, so all ET_ functions are permanently dead.
//
// Stubs are kept per stub-don't-skip policy: they emit the correct binary symbols
// so asm-verify can pair them, and call sites in WaveManager compile without guards.

// Forward-declare the binary's Mortar::Entity so the ET_NewEntity signature
// matches the binary's mangled name (_Z12ET_NewEntityPN6Mortar6EntityEit).
// The port's Entity class lives at global scope; this forward-decl is only used
// by these defunct stubs and does not affect the port's real Entity layout.
namespace Mortar { class Entity; }

// Defunct: P2P entity tracker -- no-op stub; v1.6.1 ET_ClearKnownEntities @0x001d970c
// Clears the EntityTracker registry for a given player partition.
// Called from WaveManager::StartWave with playerIdx=-1 (clear all).
void ET_ClearKnownEntities(int playerIdx);

// Defunct: P2P entity tracker -- no-op stub; v1.6.1 ET_NewEntity @0x001d97d8
// Registers a newly-spawned entity with the EntityTracker for P2P sync.
void ET_NewEntity(Mortar::Entity* entity, int type, unsigned short id);

#endif // FN_ENTITIES_ENTITY_TRACKER_H
