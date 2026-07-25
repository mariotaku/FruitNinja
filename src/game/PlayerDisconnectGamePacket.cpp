#include "game/PlayerDisconnectGamePacket.h"
#include <cstring>

// Defunct: P2P MP disconnect packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=104)
// Base ctor stamps m_PacketType=104/m_PacketSize=sizeof(PlayerDisconnectGamePacket),
// matching the binary's ctor chain to NetworkPacket(typeId, byteSize).
PlayerDisconnectGamePacket::PlayerDisconnectGamePacket()
    : Mortar::NetworkPacket(104, sizeof(PlayerDisconnectGamePacket)) {
    memset(m_payload, 0, sizeof(m_payload));
}
