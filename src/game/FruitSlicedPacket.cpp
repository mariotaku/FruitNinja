#include "game/FruitSlicedPacket.h"

// Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012ce48
// Base ctor stamps m_PacketType=101(0x65)/m_PacketSize=sizeof(FruitSlicedPacket),
// matching the binary's ctor chain to NetworkPacket(typeId, byteSize).
FruitSlicedPacket::FruitSlicedPacket()
    : Mortar::NetworkPacket(101, sizeof(FruitSlicedPacket)),
      m_FruitId(0), m_SliceX(0), m_SliceY(0), m_SliceAngle(0.0f), m_PlayerIdx(0)
{
}

// Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012cda8
FruitSlicedPacket::FruitSlicedPacket(long fruitId, uint16_t sliceX, uint16_t sliceY, float sliceAngle, long playerIdx)
    : Mortar::NetworkPacket(101, sizeof(FruitSlicedPacket)),
      m_FruitId(fruitId), m_SliceX(sliceX), m_SliceY(sliceY), m_SliceAngle(sliceAngle), m_PlayerIdx(playerIdx)
{
}
