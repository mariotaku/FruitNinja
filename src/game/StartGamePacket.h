#ifndef FN_GAME_START_GAME_PACKET_H
#define FN_GAME_START_GAME_PACKET_H

// StartGamePacket -- P2P multiplayer game-start synchronisation packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 102, total size = 0x28 (40 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// StartGamePacket own fields start at +0x14.
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=102.

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class StartGamePacket : public Mortar::NetworkPacket {
public:
    int  m_GameMode;   // +0x14 -- game mode index
    int  m_field18;    // +0x18
    int  m_field1c;    // +0x1c
    int  m_field20;    // +0x20
    int  m_field24;    // +0x24

    // Defunct: P2P MP game-start packet -- no-op stub; binary @ 0x157b20 (id=102)
    StartGamePacket();

    // Defunct: P2P MP game-start packet -- no-op stub
    explicit StartGamePacket(int gameMode);

    virtual ~StartGamePacket() {}
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(StartGamePacket) == 0x28,
    "StartGamePacket must be 0x28 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_START_GAME_PACKET_H
