#include "game/FruitSlicedPacket.h"

// Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012ce48
FruitSlicedPacket::FruitSlicedPacket()
    : m_fruitId(0), m_field14(0), m_field16(0), m_field18(0.0f), m_field1c(0), m_field20(0)
{
}

// Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012cda8
FruitSlicedPacket::FruitSlicedPacket(long fruitId, uint16_t f14, uint16_t f16, float f18, long f1c, long f20)
    : m_fruitId(fruitId), m_field14(f14), m_field16(f16), m_field18(f18), m_field1c(f1c), m_field20(f20)
{
}
