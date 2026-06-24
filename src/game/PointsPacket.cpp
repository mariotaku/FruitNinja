#include "game/PointsPacket.h"

// Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=100)
PointsPacket::PointsPacket()
    : m_Points(0), m_reserved14(0), m_reserved18(0), m_reserved1c(0), m_reserved20(0)
{
}

// Defunct: P2P MP points-broadcast packet -- no-op stub
PointsPacket::PointsPacket(int points)
    : m_Points(points), m_reserved14(0), m_reserved18(0), m_reserved1c(0), m_reserved20(0)
{
}
