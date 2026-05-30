#ifndef FN_GAME_FRUIT_SLICED_PACKET_H
#define FN_GAME_FRUIT_SLICED_PACKET_H

// FruitSlicedPacket -- P2P multiplayer fruit-slice broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctors @ 0x0012cda8 (parameterised), 0x0012ce48 (default).
// Polymorphic: vptr @ +0x00 (via base Mortar::NetworkPacket).
// Packet type ID = 0x65, total packet payload size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// FruitSlicedPacket own fields start at +0x14.

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class FruitSlicedPacket : public Mortar::NetworkPacket {
public:
    long     m_fruitId;   // +0x14 — ID of sliced fruit
    uint16_t m_field18;   // +0x18 — packet payload field (slice coords / angle)
    uint16_t m_field1a;   // +0x1a
    float    m_field1c;   // +0x1c
    long     m_field20;   // +0x20 — player ID / field

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; binary @ 0x0012ce48
    FruitSlicedPacket();

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; binary @ 0x0012cda8
    FruitSlicedPacket(long fruitId, uint16_t f18, uint16_t f1a, float f1c, long f20);

    virtual ~FruitSlicedPacket() {}
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(FruitSlicedPacket) == 36,
    "FruitSlicedPacket must be 36 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_FRUIT_SLICED_PACKET_H
