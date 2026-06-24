#ifndef FN_GAME_START_GAME_PACKET_H
#define FN_GAME_START_GAME_PACKET_H

// StartGamePacket -- P2P multiplayer game-start synchronisation packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 103 (case 0x67), total size = 0x1c (28 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x0f (16 bytes).
// StartGamePacket own fields start at +0x10 (12 bytes of own fields).
// Binary ctor @ v1.6.1 StartGamePacket::StartGamePacket @ 0x00158dc0:
//   +0x10  int32  m_Flags     (init 0x18bb8 = game-mode flags bitmask)
//   +0x14  int32  m_GameSeed  (init 0)
//   +0x18  int32  m_field18   (init 0)
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=103 (case 0x67).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class StartGamePacket : public Mortar::NetworkPacket {
public:
    int  m_Flags;        // +0x10 -- game-mode flags bitmask; init 0x18bb8
    int  m_GameSeed;     // +0x14 -- random seed for game sync; init 0
    int  m_reserved18;   // +0x18 -- no ctor-set / serialize site in binary; purpose unknown

    // Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=103 / case 0x67)
    StartGamePacket();

    // Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=103 / case 0x67)
    explicit StartGamePacket(int flags);

    virtual ~StartGamePacket() {}
};

#if defined(__bada__)
static_assert(sizeof(StartGamePacket) == 0x1c,
    "StartGamePacket must be 0x1c bytes on ARM32/Bada");
#endif

#endif // FN_GAME_START_GAME_PACKET_H
