#include "game/PointsPacket.h"

// Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x157b20 (id=100)
PointsPacket::PointsPacket()
    : m_Points(0), m_reserved18(0), m_reserved1c(0), m_reserved20(0)
{
}

// Defunct: P2P MP points-broadcast packet -- no-op stub; v1.6.1 binary @ 0x00158710
PointsPacket::PointsPacket(int points, int p18, int p1c, int p20)
    : m_Points(points), m_reserved18(p18), m_reserved1c(p1c), m_reserved20(p20)
{
}

// MP-revival: real wire serialisation; v1.6.1 PointsPacket::Serialize @ 0x001585f8
void PointsPacket::Serialize(Mortar::ByteWriter& w) const {
    NetworkPacket::WriteHeader(w);
    w.I32(m_Points);
    w.I32(m_reserved18);
    w.I32(m_reserved1c);
    w.I32(m_reserved20);
}

// MP-revival: real wire deserialisation (inverse of Serialize above)
void PointsPacket::Deserialize(Mortar::ByteReader& r) {
    NetworkPacket::ReadHeader(r);
    m_Points = r.I32();
    m_reserved18 = r.I32();
    m_reserved1c = r.I32();
    m_reserved20 = r.I32();
}
