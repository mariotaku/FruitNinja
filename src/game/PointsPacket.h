#ifndef FN_GAME_POINTS_PACKET_H
#define FN_GAME_POINTS_PACKET_H

// PointsPacket -- P2P multiplayer score broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 100, total size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// PointsPacket own fields start at +0x14.
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=100.

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class PointsPacket : public Mortar::NetworkPacket {
public:
    int  m_Points;    // +0x14 -- score points
    int  m_field18;   // +0x18
    int  m_field1c;   // +0x1c
    int  m_field20;   // +0x20

    // Defunct: P2P MP points-broadcast packet -- no-op stub; binary @ 0x157b20 (id=100)
    PointsPacket();

    // Defunct: P2P MP points-broadcast packet -- no-op stub
    explicit PointsPacket(int points);

    virtual ~PointsPacket() {}
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(PointsPacket) == 0x24,
    "PointsPacket must be 0x24 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_POINTS_PACKET_H
