#ifndef FN_GAME_START_GAME_PACKET_H
#define FN_GAME_START_GAME_PACKET_H

// StartGamePacket -- P2P multiplayer game-start synchronisation packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 103 (case 0x67), total size = 0x1c (28 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// StartGamePacket own fields start at +0x14 (8 bytes of own fields).
// Binary ctor @ v1.6.1 StartGamePacket::StartGamePacket @ 0x00158dc0:
//   +0x14  int32  m_Flags     (always init 0x18bb8 = game-mode flags bitmask)
//   +0x18  int32  m_GameSeed  (init from ctor param, or 0 for default ctor)
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=103 (case 0x67).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class StartGamePacket : public Mortar::NetworkPacket {
public:
    int  m_Flags;      // +0x14 -- game-mode flags bitmask; always init 0x18bb8
    int  m_GameSeed;   // +0x18 -- random seed for game sync; init from ctor param

    // Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
    StartGamePacket();

    // Defunct: P2P MP game-start packet -- no-op stub; v1.6.1 binary @ 0x00158dc0
    explicit StartGamePacket(int gameSeed);

    virtual ~StartGamePacket() {}

    // MP-revival: wire (de)serialisation; v1.6.1 StartGamePacket::StartGamePacket @ 0x00158dc0.
    // Payload order: m_Flags (i32), m_GameSeed (i32).
    virtual void Serialize(Mortar::ByteWriter& w) const;
    virtual void Deserialize(Mortar::ByteReader& r);
};

#if defined(__bada__)
static_assert(sizeof(StartGamePacket) == 0x1c,
    "StartGamePacket must be 0x1c bytes on ARM32/Bada");
#endif

#endif // FN_GAME_START_GAME_PACKET_H
