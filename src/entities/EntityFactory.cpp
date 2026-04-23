#include "EntityFactory.h"

#include "Entity.h"
#include "Fruit.h"
#include "Bomb.h"
#include "Coin.h"
#include "BombBlast.h"

#include <cstdio>

// CreateEntity — binary-faithful port of 0x0017421c.
//
// Binary pseudocode:
//   Entity* CreateEntity(long entityType) {
//       switch (entityType) {
//       case 0:  return new Fruit();        // 0x118 bytes
//       case 1:  return new Bomb();         // 0xb0  bytes
//       case 2:  return new Coin();         // 0x94  bytes
//       case 3:  return new SlashEntity();  // 0x184 bytes
//       case 4:  return new BombBlast();    // 0x70  bytes
//       default: return NULL;
//       }
//   }
//
// Type 3 (SlashEntity) is the single documented divergence: the port
// owns a single `g_pSlashEntity` constructed from GameInit and does not
// pool SlashEntity through ActorManager. The factory returns NULL on
// type 3 so a stray ActorManager::Add(3, true) fails loudly instead of
// silently allocating a second unmanaged slash. If/when the port
// transitions to pool-managed SlashEntity, this branch becomes
// `return new SlashEntity()` like the binary.
//
// Analysed: 2026-04-23T01:30

Entity* CreateEntity(int entityType) {
    switch (entityType) {
    case 0:  return new Fruit();
    case 1:  return new Bomb();
    case 2:  return new Coin();
    case 3:
        // Port divergence — see header comment.
        fprintf(stderr,
                "CreateEntity: type 3 (SlashEntity) is not pooled in the "
                "port; use g_pSlashEntity. Returning NULL.\n");
        return NULL;
    case 4:  return new BombBlast();
    default:
        fprintf(stderr, "CreateEntity: unknown entity type %d\n", entityType);
        return NULL;
    }
}
