#ifndef FN_GAME_FRUIT_SLICED_PACKET_H
#define FN_GAME_FRUIT_SLICED_PACKET_H

// FruitSlicedPacket -- P2P multiplayer fruit-slice broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctors @ 0x0012cda8 (parameterised), 0x0012ce48 (default).
// Polymorphic: vptr @ +0x00 (via base Mortar::NetworkPacket).
// Packet type ID = 0x65, total packet payload size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x0f (16 bytes).
// FruitSlicedPacket own fields start at +0x10 (20 bytes of own fields).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class FruitSlicedPacket : public Mortar::NetworkPacket {
public:
    long     m_fruitId;   // +0x10 -- ID of sliced fruit
    uint16_t m_field14;   // +0x14 -- packet payload field (slice coords / angle)
    uint16_t m_field16;   // +0x16
    float    m_field18;   // +0x18
    long     m_field1c;   // +0x1c -- player ID / field
    long     m_field20;   // +0x20

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; binary @ 0x0012ce48
    FruitSlicedPacket();

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; binary @ 0x0012cda8
    FruitSlicedPacket(long fruitId, uint16_t f14, uint16_t f16, float f18, long f1c, long f20);

    virtual ~FruitSlicedPacket() {}
};

#if defined(__bada__)
static_assert(sizeof(FruitSlicedPacket) == 36,
    "FruitSlicedPacket must be 36 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_FRUIT_SLICED_PACKET_H
