#include "EntityFactory.h"

#include "Entity.h"
#include "Fruit.h"
#include "Bomb.h"
#include "Coin.h"
#include "BombBlast.h"
#include "SlashEntity.h"

#include <cstdio>

// CreateEntity — binary-faithful port of 0x0017421c.
//
// Binary pseudocode:
//   Mortar::Entity* CreateEntity(long entityType) {
//       switch (entityType) {
//       case 0:  return new Fruit();        // 0x118 bytes
//       case 1:  return new Bomb();         // 0xb0  bytes
//       case 2:  return new Coin();         // 0x94  bytes
//       case 3:  return new SlashEntity();  // 0x184 bytes
//       case 4:  return new BombBlast();    // 0x70  bytes
//       default: return nullptr;
//       }
//   }
//
// Analysed: 2026-04-23T01:30

Mortar::Entity* CreateEntity(int entityType) {
    switch (entityType) {
    case 0:  return new Fruit();
    case 1:  return new Bomb();
    case 2:  return new Coin();
    case 3:  return new SlashEntity();
    case 4:  return new BombBlast();
    default:
        fprintf(stderr, "CreateEntity: unknown entity type %d\n", entityType);
        return nullptr;
    }
}
