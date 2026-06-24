#ifndef FN_GAME_POINTS_PACKET_H
#define FN_GAME_POINTS_PACKET_H

// PointsPacket -- P2P multiplayer score broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 100, total size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x0f (16 bytes).
// PointsPacket own fields start at +0x10 (20 bytes of own fields).
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=100.

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class PointsPacket : public Mortar::NetworkPacket {
public:
    // Wire fields in binary serialize order (PointsPacket::Serialize @ 0x001585f8,
    // 4-arg ctor @ 0x00158710). m_field20 has no ctor/serialize site.
    int  m_Points;        // +0x10 -- score points (WriteInt32; ctor param 1)
    int  m_reserved14;    // +0x14 -- WriteInt32; ctor param 2; purpose unknown
    int  m_reserved18;    // +0x18 -- WriteInt32; ctor param 3; purpose unknown
    int  m_reserved1c;    // +0x1c -- WriteInt32; ctor param 4; purpose unknown
    int  m_reserved20;    // +0x20 -- no ctor/serialize site; purpose unknown

    // Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=100)
    PointsPacket();

    // Defunct: P2P MP points-broadcast packet -- no-op stub
    explicit PointsPacket(int points);

    virtual ~PointsPacket() {}
};

#if defined(__bada__)
static_assert(sizeof(PointsPacket) == 0x24,
    "PointsPacket must be 0x24 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_POINTS_PACKET_H
