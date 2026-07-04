#include "EntityTracker.h"

// Defunct: P2P entity tracker -- no-op stub; v1.6.1 ET_ClearKnownEntities @0x001d970c
void ET_ClearKnownEntities(int playerIdx) {
    (void)playerIdx;
}

// Defunct: P2P entity tracker -- no-op stub; v1.6.1 ET_NewEntity @0x001d97d8
void ET_NewEntity(Mortar::Entity* entity, int type, unsigned short id) {
    (void)entity;
    (void)type;
    (void)id;
}
