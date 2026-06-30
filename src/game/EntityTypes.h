#ifndef FN_GAME_ENTITY_TYPES_H
#define FN_GAME_ENTITY_TYPES_H

// Entity type IDs — values from binary DAT_002d8ce4 (v1.6.1 HashTypeConvert @0x001d8e64).
// Match the switch cases in CreateEntity @0x001d8fec.
enum EntityType {
    ENTITY_FRUIT    = 0,
    ENTITY_BOMB     = 1,
    ENTITY_COIN     = 2,
    ENTITY_SLASH    = 3,
    ENTITY_BLAST    = 4,
    ENTITY_JIBLET   = 5,
    ENTITY_FRUITRAY = 6
};

// ASM-spec v1.6.1 HashTypeConvert @ 0x001d8e64:
//   Maps entity-type name hashes to integer type IDs (0-6, see EntityType enum above).
//   Static 7-entry table {hash, typeId, isValid}; hash fields lazily computed on first call
//   via StringHash("fruit") / StringHash("bomb") / etc. (binary uses __cxa_guard).
//   Returns typeId + found_out=true on hit; -1 + found_out=false on miss.
//   Registered as Mortar::ActorManager::RegisterHashConverter delegate in GameInit.
long HashTypeConvert(unsigned long hash, bool& found_out);

#endif // FN_GAME_ENTITY_TYPES_H
