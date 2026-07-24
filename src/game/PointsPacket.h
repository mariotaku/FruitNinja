#ifndef FN_GAME_POINTS_PACKET_H
#define FN_GAME_POINTS_PACKET_H

// PointsPacket -- P2P multiplayer score broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Packet type ID = 100, total size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// PointsPacket own fields start at +0x14 (16 bytes of own fields).
// Binary: PacketFactory::Create @ 0x157b20 allocates this for id=100.
// Binary ctor @ 0x00158710 is 4-arg: (this, int points, int p18, int p1c, int p20).
//   r1->m_Points@+0x14, r2->m_reserved18@+0x18, r3->m_reserved1c@+0x1c, stack->m_reserved20@+0x20

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class PointsPacket : public Mortar::NetworkPacket {
public:
    // Wire fields in binary serialize order (PointsPacket::Serialize @ 0x001585f8,
    // 4-arg ctor @ 0x00158710).
    int  m_Points;        // +0x14 -- score points (WriteInt32; ctor param 1)
    int  m_reserved18;    // +0x18 -- WriteInt32; ctor param 2; purpose unknown
    int  m_reserved1c;    // +0x1c -- WriteInt32; ctor param 3; purpose unknown
    int  m_reserved20;    // +0x20 -- WriteInt32; ctor param 4 (from stack); purpose unknown

    // Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=100)
    PointsPacket();

    // Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x00158710
    PointsPacket(int points, int p18, int p1c, int p20);

    virtual ~PointsPacket() {}

    // MP-revival: wire (de)serialisation; v1.6.1 PointsPacket::Serialize @ 0x001585f8.
    // Payload order matches ctor param order: m_Points, m_reserved18, m_reserved1c, m_reserved20.
    virtual void Serialize(Mortar::ByteWriter& w) const;
    virtual void Deserialize(Mortar::ByteReader& r);
};

#if defined(__bada__)
static_assert(sizeof(PointsPacket) == 0x24,
    "PointsPacket must be 0x24 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_POINTS_PACKET_H
