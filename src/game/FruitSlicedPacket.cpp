#include "game/FruitSlicedPacket.h"

// Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012ce48
FruitSlicedPacket::FruitSlicedPacket()
    : m_FruitId(0), m_SliceX(0), m_SliceY(0), m_SliceAngle(0.0f), m_PlayerIdx(0)
{
}

// Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012cda8
FruitSlicedPacket::FruitSlicedPacket(long fruitId, uint16_t sliceX, uint16_t sliceY, float sliceAngle, long playerIdx)
    : m_FruitId(fruitId), m_SliceX(sliceX), m_SliceY(sliceY), m_SliceAngle(sliceAngle), m_PlayerIdx(playerIdx)
{
}

// MP-revival: real wire serialisation; v1.6.1 FruitSlicedPacket::Serialize @ 0x00156f80
void FruitSlicedPacket::Serialize(Mortar::ByteWriter& w) const {
    NetworkPacket::WriteHeader(w);
    w.I32(static_cast<int32_t>(m_FruitId));
    w.U16(m_SliceX);
    w.U16(m_SliceY);
    w.F32(m_SliceAngle);
    w.I32(static_cast<int32_t>(m_PlayerIdx));
}

// MP-revival: real wire deserialisation (inverse of Serialize above)
void FruitSlicedPacket::Deserialize(Mortar::ByteReader& r) {
    NetworkPacket::ReadHeader(r);
    m_FruitId = r.I32();
    m_SliceX = r.U16();
    m_SliceY = r.U16();
    m_SliceAngle = r.F32();
    m_PlayerIdx = r.I32();
}
