#include "EntityFactory.h"

#include "Entity.h"
#include "Fruit.h"
#include "Bomb.h"
#include "Coin.h"
#include "BombBlast.h"
#include "Jiblet.h"
#include "SlashEntity.h"
#include "debug/Logger.h"

#include <cstdio>

// CreateEntity — binary-faithful port of _Z12CreateEntityl @0x001d8fec (v1.6.1).
//
// Binary pseudocode:
//   Mortar::Entity* CreateEntity(long entityType) {
//       switch (entityType) {
//       case 0:  return new Fruit();        // 0x18C bytes
//       case 1:  return new Bomb();         // 0xB0  bytes
//       case 2:  return new Coin();         // 0x94  bytes
//       case 3:  return new SlashEntity();  // 0x188 bytes
//       case 4:  return new BombBlast();    // 0x70  bytes
//       case 5:  return new Jiblet();       // 0xB0  bytes (memset 0xB0 first)
//       case 6:  return new FruitRay();     // (unported)
//       default: return nullptr;
//       }
//   }

Mortar::Entity* CreateEntity(long entityType) {
    switch (entityType) {
    case 0:  return new Fruit();
    case 1:  return new Bomb();
    case 2:  return new Coin();
    case 3:  return new SlashEntity();
    case 4:  return new BombBlast();
    case 5:  return new Jiblet();
    // TODO: v1.6.1 CreateEntity @0x001d8fec case 6 FruitRay (unported)
    default:
        LOG_WARN("ENTITYFACTORY", "unknown entity type %ld", entityType);
        return nullptr;
    }
}
