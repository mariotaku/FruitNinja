#include "game/PointsPacket.h"

// Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=100)
// Base ctor stamps m_PacketType=100/m_PacketSize=sizeof(PointsPacket),
// matching the binary's ctor chain to NetworkPacket(typeId, byteSize).
PointsPacket::PointsPacket()
    : Mortar::NetworkPacket(100, sizeof(PointsPacket)),
      m_Points(0), m_reserved18(0), m_reserved1c(0), m_reserved20(0)
{
}

// Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x00158710
PointsPacket::PointsPacket(int points, int p18, int p1c, int p20)
    : Mortar::NetworkPacket(100, sizeof(PointsPacket)),
      m_Points(points), m_reserved18(p18), m_reserved1c(p1c), m_reserved20(p20)
{
}
