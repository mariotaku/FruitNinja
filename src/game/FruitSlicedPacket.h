#ifndef FN_GAME_FRUIT_SLICED_PACKET_H
#define FN_GAME_FRUIT_SLICED_PACKET_H

// FruitSlicedPacket -- P2P multiplayer fruit-slice broadcast packet.
// Defunct: P2P MP -- stub per stub-don't-skip policy.
//
// Binary ctors @ 0x0012cda8 (parameterised), 0x0012ce48 (default).
// Polymorphic: vptr @ +0x00 (via base Mortar::NetworkPacket).
// Packet type ID = 0x65, total packet payload size = 0x24 (36 bytes).
// Mortar::NetworkPacket base occupies +0x00..+0x13 (20 bytes).
// FruitSlicedPacket own fields start at +0x14 (16 bytes of own fields).

#include "engine/network/NetworkPacket.h"
#include <cstdint>

class FruitSlicedPacket : public Mortar::NetworkPacket {
public:
    // Wire fields in binary serialize order (FruitSlicedPacket::Serialize @ 0x00156f80,
    // ctor @ 0x001570b0).
    long     m_FruitId;     // +0x14 -- ID of sliced fruit (WriteInt32; ctor param 1)
    uint16_t m_SliceX;      // +0x18 -- slice point X, 16-bit (WriteInt16; ctor param 2)
    uint16_t m_SliceY;      // +0x1a -- slice point Y, 16-bit (WriteInt16; ctor param 3)
    float    m_SliceAngle;  // +0x1c -- slice direction/angle (WriteDec32 float; ctor param 4)
    long     m_PlayerIdx;   // +0x20 -- player/blade slot (WriteInt32; ctor param 5)

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012ce48
    FruitSlicedPacket();

    // Defunct: P2P MP slice-broadcast packet -- no-op stub; v1.6.1 binary @ 0x0012cda8
    FruitSlicedPacket(long fruitId, uint16_t sliceX, uint16_t sliceY, float sliceAngle, long playerIdx);

    virtual ~FruitSlicedPacket() {}

    // MP-revival: wire (de)serialisation; v1.6.1 FruitSlicedPacket::Serialize @ 0x00156f80.
    // Payload order: m_FruitId (i32), m_SliceX (u16), m_SliceY (u16), m_SliceAngle (f32), m_PlayerIdx (i32).
    virtual void Serialize(Mortar::ByteWriter& w) const;
    virtual void Deserialize(Mortar::ByteReader& r);
};

#if defined(__bada__)
static_assert(sizeof(FruitSlicedPacket) == 36,
    "FruitSlicedPacket must be 36 bytes on ARM32/Bada");
#endif

#endif // FN_GAME_FRUIT_SLICED_PACKET_H
