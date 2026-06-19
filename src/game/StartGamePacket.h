#ifndef FN_GAME_START_GAME_PACKET_H
#define FN_GAME_START_GAME_PACKET_H

// StartGamePacket -- P2P multiplayer game-start synchronisation packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 103 (case 0x67), total size = 0x1c (28 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// StartGamePacket own fields start at +0x14.
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=103 (case 0x67).
// NOTE: base is currently ported at 0x14 (20 bytes); true binary base is 0x10 (16 bytes).
// The latent base-size bug (NetworkPacket 0x14 vs binary 0x10) is tracked separately.

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class StartGamePacket : public Mortar::NetworkPacket {
public:
    int  m_GameMode;   // +0x14 -- game mode index
    int  m_field18;    // +0x18

    // Defunct: P2P MP game-start packet -- no-op stub; binary @ 0x157b20 (id=103 / case 0x67)
    StartGamePacket();

    // Defunct: P2P MP game-start packet -- no-op stub; binary @ 0x157b20 (id=103 / case 0x67)
    explicit StartGamePacket(int gameMode);

    virtual ~StartGamePacket() {}
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(StartGamePacket) == 0x1c,
    "StartGamePacket must be 0x1c bytes on ARM32/Bada");
#endif

#endif // FN_GAME_START_GAME_PACKET_H
