// Defunct: P2P packet hierarchy -- stub; v1.6.1 binary @ 0x157b20 (PacketFactory).
// Binary switches on typeId to allocate: id=100->PointsPacket, id=101->FruitSlicedPacket,
// id=102->WaveSyncPacket, id=103->StartGamePacket, id=104->PlayerDisconnectGamePacket.

#include "game/PacketFactory.h"
#include "engine/network/NetworkPacket.h"
#include "game/PointsPacket.h"
#include "game/FruitSlicedPacket.h"
#include "game/StartGamePacket.h"
#include "game/WaveSyncPacket.h"
#include "game/PlayerDisconnectGamePacket.h"

// Defunct: P2P packet hierarchy -- no-op stub; v1.6.1 binary @ 0x157b20
Mortar::NetworkPacket* PacketFactory::Create(Mortar::NetworkPacket* src) {
    if (!src) return 0;
    switch (src->m_PacketType) {
        case 100: return new PointsPacket();
        case 101: return new FruitSlicedPacket();
        case 102: return new WaveSyncPacket();
        case 103: return new StartGamePacket();
        case 104: return new PlayerDisconnectGamePacket();
        default:  return 0;
    }
}
