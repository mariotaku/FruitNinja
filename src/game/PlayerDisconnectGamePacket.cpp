#include "game/PlayerDisconnectGamePacket.h"
#include <cstring>

// Defunct: P2P MP disconnect packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=104)
PlayerDisconnectGamePacket::PlayerDisconnectGamePacket() {
    memset(m_payload, 0, sizeof(m_payload));
}
