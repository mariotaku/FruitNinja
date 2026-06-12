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

// CreateEntity — binary-faithful port of 0x001d90f4 (v1.6.1).
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
    case 5:  return new Jiblet();
    default:
        LOG_WARN("ENTITYFACTORY", "unknown entity type %d", entityType);
        return nullptr;
    }
}
