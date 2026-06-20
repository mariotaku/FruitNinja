#ifndef FN_GAME_PLAYER_DISCONNECT_GAME_PACKET_H
#define FN_GAME_PLAYER_DISCONNECT_GAME_PACKET_H

// PlayerDisconnectGamePacket -- P2P multiplayer player-disconnect broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 104, total size = 0x58 (88 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x0f (16 bytes).
// PlayerDisconnectGamePacket own fields start at +0x10 (72 bytes of payload).
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=104.

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class PlayerDisconnectGamePacket : public Mortar::NetworkPacket {
public:
    // Own fields from +0x10 to +0x57 (72 bytes of payload)
    uint8_t m_payload[72]; // +0x10..+0x57

    // Defunct: P2P MP disconnect packet -- no-op stub; binary @ 0x157b20 (id=104)
    PlayerDisconnectGamePacket();

    virtual ~PlayerDisconnectGamePacket() {}
};

#if defined(__bada__)
static_assert(sizeof(PlayerDisconnectGamePacket) == 0x58,
    "PlayerDisconnectGamePacket must be 0x58 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_PLAYER_DISCONNECT_GAME_PACKET_H
