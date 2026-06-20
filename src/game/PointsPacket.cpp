#include "game/PointsPacket.h"

// Defunct: P2P MP points-broadcast packet -- no-op stub; binary @ 0x157b20 (id=100)
PointsPacket::PointsPacket()
    : m_Points(0), m_field14(0), m_field18(0), m_field1c(0), m_field20(0)
{
}

// Defunct: P2P MP points-broadcast packet -- no-op stub
PointsPacket::PointsPacket(int points)
    : m_Points(points), m_field14(0), m_field18(0), m_field1c(0), m_field20(0)
{
}
