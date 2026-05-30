#include "game/FruitSlicedPacket.h"

// Defunct: P2P MP slice-broadcast packet -- no-op stub; binary @ 0x0012ce48
FruitSlicedPacket::FruitSlicedPacket()
    : m_fruitId(0), m_field18(0), m_field1a(0), m_field1c(0.0f), m_field20(0)
{
}

// Defunct: P2P MP slice-broadcast packet -- no-op stub; binary @ 0x0012cda8
FruitSlicedPacket::FruitSlicedPacket(long fruitId, uint16_t f18, uint16_t f1a, float f1c, long f20)
    : m_fruitId(fruitId), m_field18(f18), m_field1a(f1a), m_field1c(f1c), m_field20(f20)
{
}
